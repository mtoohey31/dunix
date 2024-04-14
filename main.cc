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

enum SortMetric {
  Nar,
  Closure,
  RemovalImpact,
  References,
  Referrers,
};

struct Args : public argparse::Args {
  bool &version = flag("v,version", "Display version.");
  bool &fullPath = flag("f,full-path", "Display full store paths.");
  SortMetric &sortMetric = flag("s,sort", "Metric by which to sort referrers.")
                               .set_default(RemovalImpact);
  string &path =
      arg("path", "The store path to display disk usage breakdown for.")
          .set_default("result");

  virtual void welcome() override {
    cout << "Disk usage breakdowns for Nix store paths.\n\n";
  }

  virtual void help() override {
    argparse::Args::help();
    cout << "\nMetrics:\n"
            "         nar size : The size of the files within the store path "
            "itself. More specifically, the size of the output of nix-store "
            "--dump.\n"
         << "     closure size : The sum of the nar size metric for the store "
            "path's closure, which includes the store path itself, and all "
            "store paths referenced (directly or transitively) by it.\n"
         << "   removal impact : The space that would be saved from the root "
            "store path's closure if this store path's parent no longer "
            "depended directly on it. This is 0 if the store path has more "
            "than one referrer in the root's closure, because eliminating its "
            "parent's reference won't impact the size since the root will "
            "still depend on it through another referrer. Otherwise, it is the "
            "sum of the nar size metric for everything in the store path's "
            "closure that has no referrers outside of the closure.\n"
         << "       references : The number of store paths that this one "
            "references.\n"
         << "        referrers : The number of store paths that reference this "
            "one.\n";
  }
};

struct Vertex {
  StorePath name;
  uint64_t narSize; // NOTE: 0 means unknown, might want to track and display?

  optional<SortMetric> metric;
  vector<Vertex *>::size_type selected = 0;
  vector<Vertex *> references; // Nodes that this one refers to.

  vector<Vertex *> referrers; // Nodes that refer to this one.

  optional<uint64_t> removalImpact_;
  optional<uint64_t> closureSize_;

  Vertex(const StorePath &name_) noexcept : name(name_) {}
  Vertex(StorePath name_, Vertex *referrer) noexcept
      : name(name_), referrers(vector<Vertex *>{referrer}) {}

  void shiftSelected(int64_t by) noexcept {
    if (by > 0 && selected + by >= references.size())
      selected = references.size() - 1;
    else if (by < 0 && selected < -by)
      selected = 0;
    else
      selected += by;
  }

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

  vector<Vertex *> sortedReferences(SortMetric by) noexcept {
    if (metric && *metric == by)
      return references;

    function<bool(Vertex *, Vertex *)> comp;
    switch (by) {
    case RemovalImpact:
      comp = [](Vertex *v1, Vertex *v2) {
        if (v1->removalImpact() > v2->removalImpact())
          return true;

        if (v1->removalImpact() < v2->removalImpact())
          return false;

        // Use nar as fallback, since zero impact is common.
        return v1->narSize > v2->narSize;
      };
      break;
    case Nar:
      comp = [](Vertex *v1, Vertex *v2) { return v1->narSize > v2->narSize; };
      break;
    case Closure:
      comp = [](Vertex *v1, Vertex *v2) {
        return v1->closureSize() > v2->closureSize();
      };
      break;
    case References:
      comp = [](Vertex *v1, Vertex *v2) {
        return v1->references.size() > v2->references.size();
      };
      break;
    case Referrers:
      comp = [](Vertex *v1, Vertex *v2) {
        return v1->referrers.size() > v2->referrers.size();
      };
      break;
    }

    stable_sort(references.begin(), references.end(), comp);

    metric = by;
    return references;
  }

  Elements line(function<string(StorePath)> formatPath) {
    return vector{text(formatPath(name)),
                  text(showBytes(narSize)),
                  text(showBytes(closureSize())),
                  text(showBytes(removalImpact())),
                  text(to_string(references.size())),
                  text(to_string(referrers.size()))};
  }
};

class BreakdownComponentBase : public ComponentBase {
  ftxui::Closure exit;
  vector<Vertex *> heirarchy;
  bool fullPath;
  SortMetric sortMetric;

public:
  BreakdownComponentBase(const string &path, ftxui::Closure exit_,
                         bool fullPath_, SortMetric sortMetric_)
      : exit(exit_), fullPath(fullPath_), sortMetric(sortMetric_) {
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

  virtual ~BreakdownComponentBase() {
    set<const Vertex *> vertices;
    queue<const Vertex *> queue;
    queue.push(heirarchy.front());

    while (!queue.empty()) {
      const Vertex *v = queue.front();
      queue.pop();

      vertices.emplace(v);
      for (const Vertex *v : v->references) {
        if (vertices.contains(v))
          continue;

        queue.emplace(v);
      }
    }

    for (const Vertex *v : vertices)
      delete v;
  }

  struct FormatPath {
    bool fullPath;
    string operator()(StorePath s) {
      return fullPath ? string("/nix/store").append(s.to_string())
                      : string(s.name());
    }
  };

  virtual Element Render() noexcept override {
    Element path = paragraph(transform_reduce(
        heirarchy.begin() + 1, heirarchy.end(),
        string(FormatPath{fullPath}(heirarchy.front()->name)),
        [](string s1, string s2) { return s1 + " > " + s2; },
        [&](Vertex *v) { return FormatPath{fullPath}(v->name); }));

    vector<Vertex *> references =
        heirarchy.back()->sortedReferences(sortMetric);
    vector<Elements> lines(references.size() + 1);
    lines[0] = {
        text("name"),
        hbox({text("n") | color(Color::Red), text("ar size")}),
        hbox({text("c") | color(Color::Red), text("losure size")}),
        hbox({text("removal "), text("i") | color(Color::Red), text("mpact")}),
        hbox({text("r") | color(Color::Red), text("eferences")}),
        hbox({text("R") | color(Color::Red), text("efererrs")})};
    transform(references.begin(), references.end(), lines.begin() + 1,
              [&](Vertex *v) { return v->line(FormatPath{fullPath}); });

    Table table = Table(lines);
    table.SelectCell(sortMetric + 1, 0).DecorateCells(underlined);
    table.SelectRow(0).Decorate(bold);
    table.SelectAll().SeparatorVertical();
    table.SelectColumns(1, -1).Decorate(align_right);
    table.SelectRow(0).DecorateCells(center);
    table.SelectColumn(0).Decorate(flex);
    table.SelectRow(heirarchy.back()->selected + 1)
        .Decorate(bgcolor(Color::Blue) | focus);

    return window(
        text("dunix"),
        vbox({path, separator(), table.Render() | vscroll_indicator | yframe}));
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

    if (event == Event::ArrowDown || event == Event::Character('j')) {
      heirarchy.back()->shiftSelected(1);
      return true;
    }

    if (event == Event::ArrowUp || event == Event::Character('k')) {
      heirarchy.back()->shiftSelected(-1);
      return true;
    }

    if (event == Event::Character('f')) {
      fullPath = !fullPath;
      return true;
    }

    if (event == Event::Character('i')) {
      sortMetric = RemovalImpact;
      return true;
    }

    if (event == Event::Character('n')) {
      sortMetric = Nar;
      return true;
    }

    if (event == Event::Character('c')) {
      sortMetric = Closure;
      return true;
    }

    if (event == Event::Character('r')) {
      sortMetric = References;
      return true;
    }

    if (event == Event::Character('R')) {
      sortMetric = Referrers;
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

    if (event == Event::Special({4}) || event == Event::Special({6})) {
      heirarchy.back()->shiftSelected(10);
      return true;
    }

    if (event == Event::Special({21}) || event == Event::Special({2})) {
      heirarchy.back()->shiftSelected(-10);
      return true;
    }

    return false;
  };
};

Component BreakdownComponent(const string &path, ftxui::Closure exit,
                             bool fullPath, SortMetric sortMetric) {
  return make_shared<BreakdownComponentBase>(path, exit, fullPath, sortMetric);
}

} // namespace dunix

int main(int argc, char *argv[]) {
  using namespace dunix;
  try {
    using dunix::Args;
    Args args = argparse::parse<Args>(argc, argv);
    if (args.version) {
      cout << VERSION << "\n";
      return 0;
    }

    ScreenInteractive screen = ScreenInteractive::Fullscreen();
    screen.SetCursor({0, 0, Screen::Cursor::Hidden});
    screen.Loop(BreakdownComponent(args.path, screen.ExitLoopClosure(),
                                   args.fullPath, args.sortMetric));
  } catch (nix::Error &e) {
    cerr << e.msg() << "\n";
    return e.status;
  } catch (exception &e) {
    cerr << e.what() << "\n";
    return 1;
  } catch (...) {
    cerr << "unknown error\n";
    return 1;
  };

  return 0;
}
