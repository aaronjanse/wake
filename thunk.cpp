#include "thunk.h"
#include "expr.h"
#include "value.h"
#include <cassert>
#include <iostream>

struct Application : public Receiver {
  std::shared_ptr<Binding> args;
  std::unique_ptr<Receiver> receiver;
  void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value);
  Application(std::shared_ptr<Binding> &&args_, std::unique_ptr<Receiver> receiver_)
   : args(std::move(args_)), receiver(std::move(receiver_)) { }
};

void Application::receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) {
  if (value->type == Exception::type) {
    Receiver::receiveM(queue, std::move(receiver), std::move(value));
  } else if (value->type != Closure::type) {
    auto exception = std::make_shared<Exception>("Attempt to apply " + value->to_str() + " which is not a Closure", args->invoker);
    Receiver::receiveM(queue, std::move(receiver), std::move(exception));
  } else {
    Closure *clo = reinterpret_cast<Closure*>(value.get());
    args->next = clo->binding;
    args->location = &clo->body->location;
    queue.emplace(clo->body, std::move(args), std::move(receiver));
  }
}

struct Primitive : public Receiver {
  std::vector<std::shared_ptr<Value> > args;
  std::unique_ptr<Receiver> receiver;
  std::shared_ptr<Binding> binding;
  Prim *prim;
  void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value);
  Primitive(std::vector<std::shared_ptr<Value> > &&args_, std::unique_ptr<Receiver> receiver_, std::shared_ptr<Binding> &&binding_, Prim *prim_)
   : args(std::move(args_)), receiver(std::move(receiver_)), binding(std::move(binding_)), prim(prim_) { }
};

static void chain_app(ThunkQueue &queue, std::unique_ptr<Receiver> receiver, Prim *prim, std::shared_ptr<Binding> &&binding, std::vector<std::shared_ptr<Value> > &&args) {
  int idx = args.size();
  if (idx == prim->args) {
    prim->fn(prim->data, std::move(receiver), std::move(binding), std::move(args));
  } else {
    Binding *arg = binding.get();
    for (int i = idx+1; i < prim->args; ++i) arg = arg->next.get();
    arg->future[0].depend(queue, std::unique_ptr<Receiver>(
      new Primitive(std::move(args), std::move(receiver), std::move(binding), prim)));
  }
}

void Primitive::receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) {
  args.emplace_back(std::move(value));
  chain_app(queue, std::move(receiver), prim, std::move(binding), std::move(args));
}

void Thunk::eval(ThunkQueue &queue)
{
  if (expr->type == VarRef::type) {
    VarRef *ref = reinterpret_cast<VarRef*>(expr);
    std::shared_ptr<Binding> *iter = &binding;
    for (int depth = ref->depth; depth; --depth)
      iter = &(*iter)->next;
    int vals = (*iter)->nargs;
    if (ref->offset >= vals) {
      DefBinding *def = reinterpret_cast<DefBinding*>((*iter)->expr);
      auto closure = std::make_shared<Closure>(def->fun[ref->offset-vals]->body.get(), *iter);
      Receiver::receiveM(queue, std::move(receiver), std::move(closure));
    } else {
      (*iter)->future[ref->offset].depend(queue, std::move(receiver));
    }
  } else if (expr->type == App::type) {
    App *app = reinterpret_cast<App*>(expr);
    auto args = std::make_shared<Binding>(nullptr, queue.stack_trace?binding:nullptr, nullptr, app, 1);
    queue.emplace(app->val.get(), binding, Binding::make_completer(args, 0));
    queue.emplace(app->fn .get(), std::move(binding), std::unique_ptr<Receiver>(
      new Application(std::move(args), std::move(receiver))));
  } else if (expr->type == Lambda::type) {
    Lambda *lambda = reinterpret_cast<Lambda*>(expr);
    auto closure = std::make_shared<Closure>(lambda->body.get(), binding);
    Receiver::receiveM(queue, std::move(receiver), std::move(closure));
  } else if (expr->type == DefBinding::type) {
    DefBinding *defbinding = reinterpret_cast<DefBinding*>(expr);
    auto defs = std::make_shared<Binding>(binding, queue.stack_trace?binding:nullptr, &defbinding->location, defbinding, defbinding->val.size());
    int j = 0;
    for (auto &i : defbinding->val)
      queue.emplace(i.get(), binding, Binding::make_completer(defs, j++));
    queue.emplace(defbinding->body.get(), std::move(defs), std::move(receiver));
  } else if (expr->type == Literal::type) {
    Literal *lit = reinterpret_cast<Literal*>(expr);
    Receiver::receiveC(queue, std::move(receiver), lit->value);
  } else if (expr->type == Prim::type) {
    Prim *prim = reinterpret_cast<Prim*>(expr);
    std::vector<std::shared_ptr<Value> > args;
    args.reserve(prim->args);
    chain_app(queue, std::move(receiver), prim, std::move(binding), std::move(args));
  } else {
    assert(0 /* unreachable */);
  }
}

void ThunkQueue::run() {
  Thunk run;
  while (!queue.empty()) {
    run = std::move(queue.front());
    std::pop_heap(queue.begin(), queue.end());
    queue.pop_back();
    run.eval(*this);
  }
}

void ThunkQueue::emplace(Expr *expr, std::shared_ptr<Binding> &&binding, std::unique_ptr<Receiver> receiver) {
  queue.emplace_back(expr, ++serial + expr->uses.load(), std::move(binding), std::move(receiver));
  std::push_heap(queue.begin(), queue.end());
}

void ThunkQueue::emplace(Expr *expr, const std::shared_ptr<Binding> &binding, std::unique_ptr<Receiver> receiver) {
  emplace(expr, std::shared_ptr<Binding>(binding), std::move(receiver));
}
