//
// Created by user on 8/15/15.
//

#ifndef PYSTON_SPLAY_TREE_H
#define PYSTON_SPLAY_TREE_H

namespace pyston {
namespace gc {
template <class T>
class node {
public:
    T key;
    node<T> *p, *l, *r;

    node(T key = T(),
        node<T> *l = NULL, node<T> *r = NULL, node<T> *p = NULL) :
    key(key), p(p), l(l), r(r)
    {
    }
    ~node() {
        delete l;
        delete r;
        l = r = NULL;
    }

    static void set_parent(node<T> *c, node<T> *p) {
        if (c) c->p = p;
    }

    static void keep_parent(node<T> *v) {
        if (!v) return;
        set_parent(v->l, v);
        set_parent(v->r, v);
    }

    static void rotate(node<T> *p, node<T> *c) {
        node<T> *g = p->p;

        if (g) {
            if (p == g->l) g->l = c;
            else           g->r = c;
        }

        if (c == p->l) p->l = c->r, c->r = p;
        else           p->r = c->l, c->l = p;

        keep_parent(c);
        keep_parent(p);

        c->p = g;
    }

    static node<T> *splay(node<T> *v) {
        if (!v) return NULL;

        node<T>* p, *g;
        while(1) {
            if (!v->p) return v;

            p = v->p;
            g = p->p;

            if (!g) {
                rotate(p, v);
                return v;
            }
            else {
                bool zigzig = g->l == p && p->l == v;

                if (zigzig) {
                    rotate(g, p);
                    rotate(p, v);
                }
                else {
                    rotate(p, v);
                    rotate(g, v);
                }
            }
        }

//        if (!v) return NULL;
//
//        if (!v->p) return v;
//
//        node<T> *p = v->p;
//        node<T> *g = p->p;
//
//        if (!g) {
//            rotate(p, v);
//            return v;
//        }
//        else {
//            bool zigzig = g->l == p && p->l == v;
//
//            if (zigzig) {
//                rotate(g, p);
//                rotate(p, v);
//            }
//            else {
//                rotate(p, v);
//                rotate(g, v);
//            }
//
//            return splay(v);
//        }
    }

    static node<T> *left_most(node<T> *v) {
        node<T> *cur = v;
        while(left(cur))
            cur = left(cur);
        return cur;
    }

    static node<T> *right_most(node<T> *v) {
        node<T> *cur = v;
        while(right(cur))
            cur = right(cur);
        return cur;
    }

    static node<T> *before(node<T> *v) {
        if (left(v))
            return right_most(left(v));
        else {
            node<T> *cur = v;
            while( cur && cur == left(parent(cur)) )
                cur = parent(cur);

            if(cur) cur = parent(cur);
            return cur;
        }
    }

    static node<T> *after(node<T> *v) {
        if (right(v))
            return left_most(right(v));
        else {
            node<T> *cur = v;
            while( cur && cur == right(parent(cur)) )
                cur = parent(cur);

            if(cur) cur = parent(cur);
            return cur;
        }
    }

    static node<T> *left(node<T> *v) {
        return v ? v->l : NULL;
    }

    static node<T> *right(node<T> *v) {
        return v ? v->r : NULL;
    }

    static node<T> *parent(node<T> *v) {
        return v ? v->p : NULL;
    }
};

/*
    equals to std::set<T>
*/
template <class T>
class splay_tree {
private:

    static node<T> *find(node<T> *v, T key) {
        if (!v) return NULL;

        while(true) {
            if      (v->key == key)        return node<T>::splay(v);
            else if (v->l && key < v->key) v = v->l;
            else if (v->r && key > v->key) v = v->r;
            else                           return node<T>::splay(v);
        }

//        if (!v) return NULL;
//
//        if      (v->key == key)        return node<T>::splay(v);
//        else if (v->l && key < v->key) return find(v->l, key);
//        else if (v->r && key > v->key) return find(v->r, key);
//
//        return node<T>::splay(v);
    }

    static bool insert(node<T> *&root, T key) {
        bool found;
        std::pair<node<T>*, node<T>*> lr = split(root, key, found);

        root = new node<T>(key, lr.first, lr.second);
        node<T>::keep_parent(root);
        return !found;
    }

    static bool remove(node<T> *&root, T key) {
        root = find(root, key);

        if (root->key != key) return false;

        node<T>::set_parent(root->l, NULL);
        node<T>::set_parent(root->r, NULL);

        node<T> *tmp = root;

        root = merge(root->l, root->r);

        // prevent memory leaks
        tmp->l = NULL, tmp->r = NULL;
        delete tmp;

        return true;
    }

    static std::pair< node<T>*, node<T>* > split(node<T> *root, T key, bool& found) {
        if (!root) return std::pair< node<T>*, node<T>* >(NULL, NULL);

        root = find(root, key);

        found = false;

        if (root->key == key) {
            node<T>::set_parent(root->l, NULL);
            node<T>::set_parent(root->r, NULL);
            found = true;
            return std::make_pair(root->l, root->r);
        }
        else if (key < root->key) {
            node<T> *l = root->l;
            root->l = NULL;
            node<T>::set_parent(l, NULL);

            return std::make_pair(l, root);
        }
        else {
            node<T> *r = root->r;
            root->r = NULL;
            node<T>::set_parent(r, NULL);

            return std::make_pair(root, r);
        }
    }

    static node<T> *merge(node<T> *&l, node<T> *&r) {
        if (!l || !r) return l ? l : r;

        r = find(r, l->key);
        r->l = l;
        l->p = r;

        return r;
    }

    node<T>* root;
    int _size;

public:

    splay_tree() {
        root = NULL;
        _size = 0;
    }
    splay_tree(const splay_tree<T>& rhs) {
        clear();
        for(auto x : rhs)
            insert(x);
    }

    splay_tree& operator = (const splay_tree<T>& rhs) {
        if (this == &rhs) return *this;

        clear();
        for(auto x : rhs)
            insert(x);

        return *this;
    }

    ~splay_tree() {
        clear();
    }

    class iterator {
    private:

        /*
            p always should be root of tree t
            ALWAYS DO SPLAY
        */
        node<T>* p;
        splay_tree<T>* t;

        iterator(splay_tree<T>* t, node<T>* p) {
            this->p = node<T>::splay(p);
            this->t = t;

            if (p) this->t->root = node<T>::splay(p);
        }

    public:
        friend class splay_tree;

        iterator() { p = NULL; t = NULL; }

        iterator & operator = (const iterator& it) {
            if (this == &it) return *this;

            p = it.p;
            t = it.t;

            return *this;
        }

        const T& operator* () {
            assert(p != NULL && "end iter\n");
            return p->key;
        }

        iterator& operator++ () {
            p = node<T>::splay(node<T>::after(p));
            if (p) t->root = p;
            return *this;
        }

        iterator& operator-- () {
            p = node<T>::splay(node<T>::before(p));
            if (p) t->root = p;
            else {
                t->root = node<T>::splay(node<T>::right_most(t->root));
                p = t->root;
            }
            return *this;
        }

        bool operator== (const iterator &rhs) { return p == rhs.p; }

        bool operator!= (const iterator &rhs) { return p != rhs.p; }
    };

    friend class iterator;

    iterator find(T x) {
        root = find(root, x);
        if (root && root->key == x)
            return iterator(this, root);
        else
            return end();
    }

    int count(T x) {
        return (find(x) != end());
    }

    int size() const { return _size; }

    void clear() {
        delete root;
        root = NULL;
        _size = 0;
    }

    inline void insert(T x) {
        _size += insert(root, x);
    }

    iterator erase(T x) {
        root = find(root, x);
        node<T>* nxt = node<T>::after(root);
        _size -= remove(root, x);

        if (!nxt) return end();

        root = node<T>::splay(nxt);

        return iterator(this, root);
    }

    inline iterator erase(iterator it) {
        return erase(it.p->key);
    }

    iterator begin() { return iterator(this, node<T>::left_most(root)); }

    iterator end() { return iterator(this, 0); }

    iterator lower_bound(T x) {
        root = find(root, x);
        node<T>* cur = root;
        while(cur && cur->key < x) {
            cur = node<T>::after(cur);
        }
        if(cur) {
            root = node<T>::splay(cur);
            return iterator(this, root);
        }
        else {
            return end();
        }
//        node<T>* tree = root;
//
//        while(tree) {
//            if (tree->key >= x && tree->l)     tree = tree->l;
//            else if (tree->key < x && tree->r) tree = tree->r;
//            else break;
//        }
//
//        root = tree = node<T>::splay(tree);
//        while (tree && tree->key < x) {
//            tree = node<T>::after(tree);
//        }
//
//        if (!tree) {
//            return end();
//        }
//        else {
//            root = node<T>::splay(tree);
//            return iterator(this, root);
//        }
    }
};

}
}

#endif //PYSTON_SPLAY_TREE_H
