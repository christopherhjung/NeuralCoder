#ifndef THORIN_PASS_PASS_H
#define THORIN_PASS_PASS_H

#include "thorin/world.h"
#include <deque>

namespace thorin {

class PassMgr;

/// All Pass%es that want to be registered in the @p PassMgr must implement this interface.
class Pass {
public:
    Pass(PassMgr& mgr)
        : mgr_(mgr)
    {}
    virtual ~Pass() {}

    PassMgr& mgr() { return mgr_; }
    World& world();
    virtual Def* rewrite(Def* nominal) { return nominal; }  ///< Rewrites @em nominal @p Def%s.
    virtual const Def* rewrite(const Def*) = 0;             ///< Rewrites @em structural @p Def%s.
    virtual void analyze(const Def*) = 0;                   ///< Invoked after the @p PassMgr has finisched @p rewrite%ing a nominal.
    virtual void new_state() = 0;                           ///< The @p PassMgr will notify all @p Pass%es if a new state has been required.
    virtual void undo(size_t u) = 0;                        ///< The @p PassMgr will notify all @p Pass%es if an undo to state @p u is required.

private:
    PassMgr& mgr_;
};

/**
 * A super optimizer.
 * See "Composing dataflow analyses and transformations" by Lerner, Grove, Chambers.
 */
class PassMgr {
public:
    static constexpr size_t No_Undo = std::numeric_limits<size_t>::max();

    PassMgr(World& world)
        : world_(world)
    {
        states_.emplace_back();
    }

    World& world() { return world_; }
    template<typename T, typename... Args>
    void create(Args&&... args) { passes_.emplace_back(std::make_unique<T>(*this, std::forward(args)...)); }
    void run();
    Def* rewrite(Def*);             ///< rewrites @em nominal @p Def%s
    const Def* rewrite(const Def*); ///< rewrites @em structural @p Def%s
    const Def* rebuild(const Def*);
    void undo(size_t u) { undo_ = std::min(undo_, u); }
    size_t num_states() const { return states_.size(); }
    Def* cur_nominal() const { return cur_nominal_; }
    Lam* cur_lam() const { return cur_nominal_->as<Lam>(); }

    std::optional<const Def*> lookup(const Def* old_def) {
        auto& old2new = cur_state().old2new;
        if (auto i = old2new.find(old_def); i != old2new.end())
            return lookup(old2new, i);
        return {};
    }

private:
    static const Def* lookup(Def2Def& old2new, Def2Def::iterator i) {
        if (auto j = old2new.find(i->second); j != old2new.end() && i != j)
            i->second = lookup(old2new, j); // path compression + transitive replacements
        return i->second;
    }

    template<class D> // D may be "Def" or "const Def"
    D* map(const Def* old_def, D* new_def) { cur_state().old2new.emplace(old_def, new_def); return new_def; }

    struct State {
        struct OrderLt { // visit basic blocks first
            bool operator()(Def* a, Def* b) { return a->type()->order() < b->type()->order(); }
        };

        State() = default;
        State(const State&) = delete;
        State(State&&) = delete;
        State& operator=(State) = delete;

        State(const State& prev, Def* nominal, Defs old_ops)
            : queue(prev.queue)
            , old2new(prev.old2new)
            , analyzed(prev.analyzed)
            , nominal(nominal)
            , old_ops(old_ops)
        {}

        std::priority_queue<Def*, std::deque<Def*>, OrderLt> queue;
        Def2Def old2new;
        DefSet analyzed;
        Def* nominal;
        Array<const Def*> old_ops;
    };

    void analyze(const Def*);
    void enqueue(Def* nominal) { cur_state().queue.push(nominal); }
    State& cur_state() { assert(!states_.empty()); return states_.back(); }
    void new_state(Def* nominal, Defs old_ops) {
        for (auto&& pass : passes_)
            pass->new_state();
        states_.emplace_back(cur_state(), nominal, old_ops);
    }

    World& world_;
    std::deque<std::unique_ptr<Pass>> passes_;
    std::deque<State> states_;
    Def* cur_nominal_;
    size_t undo_ = No_Undo;
};

}

#endif
