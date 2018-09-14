#include "heap.h"
#include "location.h"
#include "expr.h"

Receiver::~Receiver() { }

struct Completer : public Receiver {
  std::shared_ptr<Future> future;
  void receive(ThunkQueue &queue, std::shared_ptr<Value> &&value);
  Completer(std::shared_ptr<Future> &&future_) : future(future_) { }
};

void Completer::receive(ThunkQueue &queue, std::shared_ptr<Value> &&value) {
  future->value = std::move(value);
  std::unique_ptr<Receiver> iter, next;
  for (iter = std::move(future->waiting); iter; iter = std::move(next)) {
    next = std::move(iter->next);
    Receiver::receiveC(queue, std::move(iter), future->value);
  }
}

Binding::Binding(const std::shared_ptr<Binding> &next_, const std::shared_ptr<Binding> &invoker_, Location *location_, Expr *expr_, int nargs_)
 : next(next_), invoker(invoker_), future(new Future[nargs_]), location(location_), expr(expr_), nargs(nargs_) {
  ++expr->uses;
}

Binding::~Binding() {
  --expr->uses;
}

std::unique_ptr<Receiver> Binding::make_completer(const std::shared_ptr<Binding> &binding, int arg) {
  return std::unique_ptr<Receiver>(new Completer(std::shared_ptr<Future>(binding, &binding->future[arg])));
}

std::vector<Location> Binding::stack_trace(const std::shared_ptr<Binding> &binding) {
  std::vector<Location> out;
  for (Binding *i = binding.get(); i; i = i->invoker.get())
    if (i->expr->type == App::type)
      out.emplace_back(*i->location);
  return out;
}
