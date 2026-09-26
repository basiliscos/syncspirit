// main.cpp
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Tree.H>
#include <fl_draw.H>

#include <unordered_map>
#include <string>

class CheckTree : public Fl_Tree {
  public:
    CheckTree(int X, int Y, int W, int H, const char *L = nullptr) : Fl_Tree(X, Y, W, H, L) {
        Fl::add_timeout(0.0, cb_redraw, this);
    }

    // store checkbox state by item pointer
    void set_checked(Fl_Tree_Item *it, bool v) {
        checked[it] = v;
        redraw();
    }

  protected:
    bool overlay_visible_ = true;

    static void cb_redraw(void *userdata) {
        auto *self = static_cast<CheckTree *>(userdata);
        self->redraw();
    }

    void draw() override {
        Fl_Tree::draw();

        // Overlay checkboxes for visible items:
        // In this minimal demo, we just draw for the items we know we created.
        // (To make it fully generic you’d need a visible-item iteration API from your headers.)
        for (auto &kv : checked) {
            Fl_Tree_Item *it = kv.first;
            if (!it)
                continue;

            // Use item geometry (available in many FLTK 1.4.x builds).
            int ix = it->x();
            int iy = it->y();
            int iw = it->w();
            int ih = it->h();

            // Draw a small square at row start.
            int box = ih;
            if (box > 18)
                box = 18; // keep it tidy
            int bx = ix;
            int by = iy + (ih - box) / 2;

            fl_color(FL_BLACK);
            fl_rect(bx, by, box, box);

            if (kv.second) {
                // check mark
                fl_line(bx + box * 0.25, by + box * 0.55, bx + box * 0.45, by + box * 0.75);
                fl_line(bx + box * 0.45, by + box * 0.75, bx + box * 0.80, by + box * 0.25);
            }
        }
    }

  private:
    std::unordered_map<Fl_Tree_Item *, bool> checked;
};

int main(int argc, char **argv) {
    Fl::scheme("gtk+");

    Fl_Window win(600, 400, "FLTK Fl_Tree checkbox-like");
    CheckTree tree(10, 10, 580, 380);
    win.resizable(&tree);

    Fl_Tree_Item *root = tree.add(nullptr, "Root");
    Fl_Tree_Item *a = tree.add(root, "Item A");
    Fl_Tree_Item *b = tree.add(root, "Item B");
    Fl_Tree_Item *c = tree.add(root, "Item C");
    Fl_Tree_Item *d = tree.add(root, "Item D");

    tree.showroot(0);
    tree.set_checked(a, true);
    tree.set_checked(b, false);
    tree.set_checked(c, true);
    tree.set_checked(d, false);

    win.end();
    win.show(argc, argv);
    return Fl::run();
}
