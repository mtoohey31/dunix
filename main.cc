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

  Vertex(const StorePath &name_) noexcept : name(name_) {}
  Vertex(StorePath name_, Vertex *referrer) noexcept
      : name(name_), referrers(vector<Vertex *>{referrer}) {}

  uint64_t removalImpact() const noexcept {
    // TODO: Check for descendants that are only referenced by this vertex's
    // closure.
    return referrers.size() == 1 ? narSize : 0;
  }

  vector<Vertex *> sortedReferences() noexcept {
    if (sorted)
      return references;

    sort(references.begin(), references.end(),
         [](Vertex *v1, Vertex *v2) { return v1->narSize > v2->narSize; });
    sorted = true;
    return references;
  }

  vector<string> line() {
    return vector{string(name.name()), showBytes(narSize),
                  showBytes(removalImpact()), to_string(references.size()),
                  to_string(referrers.size())};
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
    lines[0] = {"name", "nar size", "removal impact", "references",
                "refererrs"};
    transform(references.begin(), references.end(), lines.begin() + 1,
              [](Vertex *v) { return v->line(); });

    Table table = Table(lines);
    table.SelectRow(0).Decorate(bold);
    table.SelectAll().SeparatorVertical();
    table.SelectColumns(1, 3).Decorate(align_right);
    table.SelectRow(0).DecorateCells(center);
    table.SelectColumn(0).Decorate(flex);
    table.SelectRow(heirarchy.back()->selected + 1)
        .Decorate(bgcolor(Color::Blue));

    // TODO: Make table vertical separator extend to bottom of screen.

    // TODO: Support scrolling.

    return window(
        text("dunix"),
        vbox({path, separator(), yframe(table.Render()) | vscroll_indicator}));
  };

  virtual bool OnEvent(Event event) noexcept override {
    if (event == Event::Escape || event == Event::Character('q')) {
      exit();
      return true;
    }

    // TODO: More vim-like vertical movement bindings.

    if ((event == Event::ArrowLeft || event == Event::Character('h')) &&
        heirarchy.size() > 1) {
      heirarchy.pop_back();
      return true;
    }

    if ((event == Event::ArrowRight || event == Event::Character('l')) &&
        !heirarchy.back()->references.empty()) {
      heirarchy.push_back(
          heirarchy.back()->references[heirarchy.back()->selected]);
      return true;
    }

    if ((event == Event::ArrowDown || event == Event::Character('j')) &&
        heirarchy.back()->selected < heirarchy.back()->references.size() - 1) {
      heirarchy.back()->selected++;
      return true;
    }

    if ((event == Event::ArrowUp || event == Event::Character('k')) &&
        heirarchy.back()->selected > 0) {
      heirarchy.back()->selected--;
      return true;
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
