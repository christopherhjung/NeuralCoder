#include "anydsl2/analyses/scope.h"

#include <algorithm>

#include "anydsl2/lambda.h"
#include "anydsl2/literal.h"
#include "anydsl2/primop.h"
#include "anydsl2/type.h"
#include "anydsl2/world.h"
#include "anydsl2/analyses/domtree.h"
#include "anydsl2/analyses/looptree.h"
#include "anydsl2/util/for_all.h"

namespace anydsl2 {

struct ScopeLess {
    bool operator () (const Lambda* l1, const Lambda* l2) const { return l1->sid() < l2->sid(); }
};

Scope::Scope(Lambda* entry)
    : world_(entry->world())
    , num_entries_(1)
    , num_exits_(-1)
{
    Lambda* entries[1] = { entry };
    analyze(entries);
    process(entries);
}

Scope::Scope(World& world, ArrayRef<Lambda*> entries)
    : world_(world)
    , num_entries_(entries.size())
{
    analyze(entries);
    process(entries);
}

Scope::Scope(World& world) 
    : world_(world)
    , num_entries_(0)
{
    size_t pass = world.new_pass();

    for_all (lambda, world.lambdas()) {
        if (!lambda->is_visited(pass))
            jump_to_param_users(pass, lambda, lambda);
    }

    std::vector<Lambda*> entries;

    for_all (lambda, world.lambdas()) {
        if (!lambda->is_visited(pass)) {
            insert(pass, lambda);
            entries.push_back(lambda);
        }
    }

    num_entries_ = entries.size();
    process(entries);
}

void Scope::analyze(ArrayRef<Lambda*> entries) {
    // identify all lambdas depending on entry
    size_t pass = world().new_pass();
    for_all (entry, entries) {
        insert(pass, entry);
        jump_to_param_users(pass, entry, 0);
    }
}

void Scope::process(ArrayRef<Lambda*> entries) {
    // number all lambdas in postorder
    size_t pass = world().new_pass();

    for_all (entry, entries)
        entry->visit_first(pass);

    size_t num = 0;
    for_all (entry, entries) {
        for_all (succ, entry->succs()) {
            if (contains(succ) && !succ->is_visited(pass))
                num = number(true, pass, succ, num);
        }
    }

    for (size_t i = entries.size(); i-- != 0;)
        entries[i]->sid_ = num++;

    assert(num <= size());
    assert(num >= 1);

    // convert postorder number to reverse postorder number
    for_all (lambda, rpo()) {
        if (lambda->is_visited(pass)) {
            lambda->sid_ = num - 1 - lambda->sid_;
        } else { // lambda is unreachable
            lambda->scope_ = 0;
            lambda->sid_ = size_t(-1);
        }
    }
    
    // sort rpo_ according to sid_ which now holds the rpo number
    std::sort(rpo_.begin(), rpo_.end(), ScopeLess());

    // discard unreachable lambdas
    rpo_.resize(num);
}

Scope::~Scope() {
    for_all (lambda, rpo_)
        lambda->scope_ = 0;
}

void Scope::jump_to_param_users(const size_t pass, Lambda* lambda, Lambda* limit) {
    for_all (param, lambda->params())
        find_user(pass, param, limit);
}

inline void Scope::find_user(const size_t pass, const Def* def, Lambda* limit) {
    if (Lambda* lambda = def->isa_lambda())
        up(pass, lambda, limit);
    else {
        if (def->visit(pass))
            return;

        for_all (use, def->uses())
            find_user(pass, use, limit);
    }
}

void Scope::up(const size_t pass, Lambda* lambda, Lambda* limit) {
    if (lambda->is_visited(pass) || (limit && limit == lambda))
        return;

    insert(pass, lambda);
    jump_to_param_users(pass, lambda, limit);

    for_all (pred, lambda->preds())
        up(pass, pred, limit);
}

size_t Scope::number(bool forwards, const size_t pass, Lambda* cur, size_t i) {
    cur->visit_first(pass);

    // for each successor in scope
    for_all (succ, forwards ? cur->succs() : cur->preds()) {
        if (contains(succ) && !succ->is_visited(pass))
            i = number(forwards, pass, succ, i);
    }

    return (cur->sid_ = i) + 1;
}

#define ANYDSL2_SCOPE_SUCC_PRED(succ) \
ArrayRef<Lambda*> Scope::succ##s(Lambda* lambda) const { \
    assert(contains(lambda));  \
    if (succ##s_.data() == 0) { \
        succ##s_.alloc(size()); \
        for_all (lambda, rpo_) { \
            Lambdas all_succ##s = lambda->succ##s(); \
            Array<Lambda*>& succ##s = succ##s_[lambda->sid()]; \
            succ##s.alloc(all_succ##s.size()); \
            size_t i = 0; \
            for_all (succ, all_succ##s) { \
                if (contains(succ)) \
                    succ##s[i++] = succ; \
            } \
            succ##s.shrink(i); \
        } \
    } \
    return succ##s_[lambda->sid()];  \
}

ANYDSL2_SCOPE_SUCC_PRED(succ)
ANYDSL2_SCOPE_SUCC_PRED(pred)

ArrayRef<Lambda*> Scope::backwards_rpo() const {
    if (!backwards_rpo_) {
        backwards_rpo_ = new Array<Lambda*>(size());

        std::vector<Lambda*> exits;

        for_all (lambda, rpo()) {
            if (num_succs(lambda) == 0)
                exits.push_back(lambda);
        }

        num_exits_ = exits.size();
    }

    return *backwards_rpo_;
}

//------------------------------------------------------------------------------

class Mangler {
public:

    Mangler(const Scope& scope, ArrayRef<size_t> to_drop, ArrayRef<const Def*> drop_with, 
           ArrayRef<const Def*> to_lift, const GenericMap& generic_map)
        : scope(scope)
        , to_drop(to_drop)
        , drop_with(drop_with)
        , to_lift(to_lift)
        , generic_map(generic_map)
        , world(scope.world())
        , pass(world.new_pass())
    {}

    Lambda* mangle();
    void mangle_body(Lambda* olambda, Lambda* nlambda);
    Lambda* mangle_head(Lambda* olambda);
    const Def* mangle(const Def* odef);
    const Def* map(const Def* def, const Def* to) {
        def->visit_first(pass);
        def->cptr = to;
        return to;
    }
    const Def* lookup(const Def* def) {
        assert(def->is_visited(pass));
        return (const Def*) def->cptr;
    }

    const Scope& scope;
    ArrayRef<size_t> to_drop;
    ArrayRef<const Def*> drop_with;
    ArrayRef<const Def*> to_lift;
    GenericMap generic_map;
    World& world;
    const size_t pass;
    Lambda* nentry;
    Lambda* oentry;
};

//------------------------------------------------------------------------------

Lambda* Mangler::mangle() {
    assert(scope.num_entries() == 1 && "TODO");
    oentry = scope.entries()[0];
    const Pi* o_pi = oentry->pi();
    Array<const Type*> nelems = o_pi->elems().cut(to_drop, to_lift.size());
    size_t offset = o_pi->elems().size() - to_drop.size();

    for (size_t i = offset, e = nelems.size(), x = 0; i != e; ++i, ++x)
        nelems[i] = to_lift[x]->type();

    const Pi* n_pi = world.pi(nelems)->specialize(generic_map)->as<Pi>();
    nentry = world.lambda(n_pi, oentry->name);

    // put in params for entry (oentry)
    // op -> iterates over old params
    // np -> iterates over new params
    //  i -> iterates over to_drop
    for (size_t op = 0, np = 0, i = 0, e = o_pi->size(); op != e; ++op) {
        const Param* oparam = oentry->param(op);
        if (i < to_drop.size() && to_drop[i] == op)
            map(oparam, drop_with[i++]);
        else {
            const Param* nparam = nentry->param(np++);
            nparam->name = oparam->name;
            map(oparam, nparam);
        }
    }

    for (size_t i = offset, e = nelems.size(), x = 0; i != e; ++i, ++x) {
        map(to_lift[x], nentry->param(i));
        nentry->param(i)->name = to_lift[x]->name;
    }

    map(oentry, oentry);
    mangle_body(oentry, nentry);

    for_all (cur, scope.rpo().slice_back(1)) {
        if (cur->is_visited(pass))
            mangle_body(cur, lookup(cur)->as_lambda());
    }

    return nentry;
}

Lambda* Mangler::mangle_head(Lambda* olambda) {
    assert(!olambda->is_visited(pass));

    Lambda* nlambda = olambda->stub(generic_map, olambda->name);
    map(olambda, nlambda);

    for_all2 (oparam, olambda->params(), nparam, nlambda->params())
        map(oparam, nparam);

    return nlambda;
}

void Mangler::mangle_body(Lambda* olambda, Lambda* nlambda) {
    Array<const Def*> ops(olambda->ops().size());
    for (size_t i = 1, e = ops.size(); i != e; ++i)
        ops[i] = mangle(olambda->op(i));

    // fold branch if possible
    if (const Select* select = olambda->to()->isa<Select>()) {
        const Def* cond = mangle(select->cond());
        if (const PrimLit* lit = cond->isa<PrimLit>())
            ops[0] = mangle(lit->value().get_u1().get() ? select->tval() : select->fval());
        else
            ops[0] = world.select(cond, mangle(select->tval()), mangle(select->fval()));
    } else
        ops[0] = mangle(olambda->to());

    ArrayRef<const Def*> nargs(ops.slice_back(1));  // new args of nlambda
    const Def* ntarget = ops.front();               // new target of nlambda

    // check whether we can optimize tail recursion
    if (ntarget == oentry) {
        bool substitute = true;
        for (size_t i = 0, e = to_drop.size(); i != e && substitute; ++i)
            substitute &= nargs[to_drop[i]] == drop_with[i];

        if (substitute)
            return nlambda->jump(nentry, nargs.cut(to_drop));
    }

    nlambda->jump(ntarget, nargs);
}

const Def* Mangler::mangle(const Def* odef) {
    if (odef->is_visited(pass))
        return lookup(odef);

    if (Lambda* olambda = odef->isa_lambda()) {
        if (scope.contains(olambda))
            return mangle_head(olambda);
        else
            return map(odef, odef);
    } else if (odef->isa<Param>())
        return map(odef, odef);

    bool is_new = false;
    const PrimOp* oprimop = odef->as<PrimOp>();
    Array<const Def*> nops(oprimop->size());
    for_all2 (&nop, nops, op, oprimop->ops()) {
        nop = mangle(op);
        is_new |= nop != op;
    }

    return map(oprimop, is_new ? world.rebuild(oprimop, nops) : oprimop);
}

//------------------------------------------------------------------------------

Lambda* Scope::clone(const GenericMap& generic_map) { 
    return mangle(Array<size_t>(), Array<const Def*>(), Array<const Def*>(), generic_map);
}

Lambda* Scope::drop(ArrayRef<const Def*> with) {
    size_t size = with.size();
    Array<size_t> to_drop(size);
    for (size_t i = 0; i != size; ++i)
        to_drop[i] = i;

    return mangle(to_drop, with, Array<const Def*>());
}

Lambda* Scope::drop(ArrayRef<size_t> to_drop, ArrayRef<const Def*> drop_with, const GenericMap& generic_map) {
    return mangle(to_drop, drop_with, Array<const Def*>(), generic_map);
}

Lambda* Scope::lift(ArrayRef<const Def*> to_lift, const GenericMap& generic_map) {
    return mangle(Array<size_t>(), Array<const Def*>(), to_lift, generic_map);
}

Lambda* Scope::mangle(ArrayRef<size_t> to_drop, ArrayRef<const Def*> drop_with, 
                       ArrayRef<const Def*> to_lift, const GenericMap& generic_map) {
    return Mangler(*this, to_drop, drop_with, to_lift, generic_map).mangle();
}

//------------------------------------------------------------------------------

const DomTree& Scope::domtree() const { return domtree_ ? *domtree_ : *(domtree_ = new DomTree(*this, true)); }
const DomTree& Scope::postdomtree() const { return postdomtree_ ? *postdomtree_ : *(postdomtree_ = new DomTree(*this, false)); }
const LoopTreeNode* Scope::looptree() const { return looptree_ ? looptree_ : looptree_ = create_loop_forest(*this); }
const LoopInfo& Scope::loopinfo() const { return loopinfo_ ? *loopinfo_ : *(loopinfo_ = new LoopInfo(*this)); }

//------------------------------------------------------------------------------

} // namespace anydsl2
