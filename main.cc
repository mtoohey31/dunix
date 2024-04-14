#include <algorithm>
#include <numeric>
#include <queue>

#include <argparse/argparse.hpp>

#include <config.h>
#include <local-store.hh>
#include <shared.hh>
#include <util.hh>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/task.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>

using namespace ftxui;
using namespace nix;
using namespace std;

namespace dunix {

struct Args : public argparse::Args {
  string &path =
      arg("path", "The path of the package to display size info for.")
          .set_default("result");
};

struct Vertex {
  StorePath name;
  uint64_t narSize; // NOTE: 0 means unknown, might want to track and display?

  bool sorted = false;
  vector<Vertex *>::size_type selected = 0;
  vector<Vertex *> references; // Nodes that this one refers to.

  vector<Vertex *> referrers; // Nodes that refer to this one.

  optional<uint64_t> removalImpact_;
  optional<uint64_t> closureSize_;

  Vertex(const StorePath &name_) noexcept : name(name_) {}
  Vertex(StorePath name_, Vertex *referrer) noexcept
      : name(name_), referrers(vector<Vertex *>{referrer}) {}

  uint64_t removalImpact() noexcept {
    if (removalImpact_)
      return *removalImpact_;

    if (referrers.size() > 1) {
      removalImpact_ = 0;
      return 0;
    }

    unordered_map<StorePath, const Vertex *> closure;
    queue<const Vertex *> queue;
    queue.push(this);

    while (!queue.empty()) {
      const Vertex *v = queue.front();
      queue.pop();

      closure.emplace(v->name, v);
      for (const Vertex *v : v->references) {
        if (closure.contains(v->name))
          continue;

        queue.emplace(v);
      }
    }

    // Won't be counted below since the closure doesn't contain the single
    // parent referrer.
    uint64_t res = narSize;
    for (pair<StorePath, const Vertex *> p : closure) {
      vector<Vertex *> referrers = p.second->referrers;
      auto pos = find_if_not(
          referrers.begin(), referrers.end(),
          [&closure](const Vertex *v) { return closure.contains(v->name); });
      if (pos == end(referrers))
        res += p.second->narSize;
    }

    removalImpact_ = res;
    return res;
  }

  uint64_t closureSize() noexcept {
    if (closureSize_)
      return *closureSize_;

    unordered_map<StorePath, const Vertex *> closure;
    queue<const Vertex *> queue;
    queue.push(this);

    while (!queue.empty()) {
      const Vertex *v = queue.front();
      queue.pop();

      closure.emplace(v->name, v);
      for (const Vertex *v : v->references) {
        if (closure.contains(v->name))
          continue;

        queue.emplace(v);
      }
    }

    uint64_t res = transform_reduce(
        closure.begin(), closure.end(), 0, plus{},
        [](pair<StorePath, const Vertex *> p) { return p.second->narSize; });
    closureSize_ = res;
    return res;
  }

  vector<Vertex *> sortedReferences() noexcept {
    if (sorted)
      return references;

    sort(references.begin(), references.end(), [](Vertex *v1, Vertex *v2) {
      if (v1->removalImpact() > v2->removalImpact())
        return true;

      if (v1->removalImpact() < v2->removalImpact())
        return false;

      return v1->narSize > v2->narSize;
    });
    sorted = true;
    return references;
  }

  vector<string> line() {
    return vector{string(name.name()),          showBytes(removalImpact()),
                  showBytes(narSize),           showBytes(closureSize()),
                  to_string(references.size()), to_string(referrers.size())};
  }
};

class BreakdownComponentBase : public ComponentBase {
  Closure exit;
  vector<Vertex *> heirarchy;

public:
  BreakdownComponentBase(const string &path, Closure exit_) : exit(exit_) {
    initNix();
    initPlugins();

    using nix::ref;
    ref<Store> store = openStore();
    StorePath root = store->followLinksToStorePath(path);

    Vertex *root_node = new Vertex(root);
    heirarchy = {root_node};
    unordered_map<StorePath, Vertex *> closure;
    closure.emplace(root, root_node);

    queue<StorePath> queue;
    queue.push(root);

    while (!queue.empty()) {
      Vertex *node = closure[queue.front()];
      ref<const ValidPathInfo> info = store->queryPathInfo(queue.front());
      queue.pop();
      node->narSize = info->narSize;

      for (const StorePath &reference : info->references) {
        if (reference == node->name)
          continue;

        if (closure.contains(reference)) {
          closure[reference]->referrers.push_back(node);
          node->references.push_back(closure[reference]);
          continue;
        }

        Vertex *reference_node = new Vertex(reference, node);
        closure.emplace(reference, reference_node);
        queue.push(reference);
        node->references.push_back(reference_node);
      }
    }
  };

  virtual Element Render() noexcept override {
    Element path = paragraph(transform_reduce(
        heirarchy.begin() + 1, heirarchy.end(),
        string(heirarchy.front()->name.name()),
        [](string s1, string s2) { return s1 + " > " + s2; },
        [](Vertex *v) { return string(v->name.name()); }));

    vector<Vertex *> references = heirarchy.back()->sortedReferences();
    vector<vector<string>> lines(references.size() + 1);
    lines[0] = {"name",         "removal impact", "nar size",
                "closure size", "references",     "refererrs"};
    transform(references.begin(), references.end(), lines.begin() + 1,
              [](Vertex *v) { return v->line(); });

    Table table = Table(lines);
    table.SelectRow(0).Decorate(bold);
    table.SelectAll().SeparatorVertical();
    table.SelectColumns(1, -1).Decorate(align_right);
    table.SelectRow(0).DecorateCells(center);
    table.SelectColumn(0).Decorate(flex);
    table.SelectRow(heirarchy.back()->selected + 1)
        .Decorate(bgcolor(Color::Blue));

    // TODO: Make table vertical separator extend to bottom of screen.

    // TODO: Support scrolling and related keybinds.

    return window(
        text("dunix"),
        vbox({path, separator(), yframe(table.Render()) | vscroll_indicator}));
  };

  virtual bool OnEvent(Event event) noexcept override {
    if (event == Event::Escape || event == Event::Character('q')) {
      exit();
      return true;
    }

    if ((event == Event::ArrowLeft || event == Event::Character('h')) &&
        heirarchy.size() > 1) {
      heirarchy.pop_back();
      return true;
    }

    vector<Vertex *>::size_type &selected = heirarchy.back()->selected;
    vector<Vertex *> &references = heirarchy.back()->references;

    if ((event == Event::ArrowRight || event == Event::Character('l') ||
         event == Event::Return) &&
        !references.empty()) {
      heirarchy.push_back(references[selected]);
      return true;
    }

    if ((event == Event::ArrowDown || event == Event::Character('j')) &&
        selected < references.size() - 1) {
      selected++;
      return true;
    }

    if ((event == Event::ArrowUp || event == Event::Character('k')) &&
        selected > 0) {
      selected--;
      return true;
    }

    if (event == Event::Character('g')) {
      selected = 0;
      return true;
    }

    if (event == Event::Character('G')) {
      selected = references.size() - 1;
      return true;
    }

    if (event == Event::Special({21}) || event == Event::Special({2})) {
      if (selected < 10)
        selected = 0;
      else
        selected -= 10;
    }

    if (event == Event::Special({4}) || event == Event::Special({6})) {
      if (selected < references.size() - 10)
        selected += 10;
      else
        selected = references.size() - 1;
    }

    return false;
  };
};

Component BreakdownComponent(const string &path, Closure exit) {
  return make_shared<BreakdownComponentBase>(path, exit);
}

} // namespace dunix

int main(int argc, char *argv[]) {
  using namespace dunix;
  try {
    using dunix::Args;
    Args args = argparse::parse<Args>(argc, argv);

    ScreenInteractive screen = ScreenInteractive::Fullscreen();
    screen.SetCursor({0, 0, Screen::Cursor::Hidden});
    screen.Loop(BreakdownComponent(args.path, screen.ExitLoopClosure()));
  } catch (nix::Error &e) {
    cerr << e.msg() << endl;
    return e.status;
  } catch (exception &e) {
    cerr << e.what() << endl;
    return 1;
  } catch (...) {
    cerr << "unknown error" << endl;
    return 1;
  };

  // TODO: Check for leaks.
  return 0;
}
