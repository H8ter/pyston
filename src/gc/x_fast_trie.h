//
// Created by user on 8/25/15.
//

#ifndef PYSTON_X_FAST_TRIE_H
#define PYSTON_X_FAST_TRIE_H

#include <cassert>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace pyston {
namespace gc {

typedef unsigned long long uint;

class x_fast_trie {
public:
    class node {
    public:
        node* l;
        node* r;
        node(node* l = 0, node* r = 0) : l(l), r(r) {}
    };

    class leaf_node : public node {
    public:
        leaf_node(uint key, node* l = 0, node* r = 0) : key(key) {
            this->l = l;
            this->r = r;
        }
        uint key;
    };

    int width;
    int cnt;
    std::vector< std::unordered_map<uint, node*> > table;
    leaf_node* leaf_list;
    std::unordered_set<void*> leaf_set;

    inline leaf_node* as_leaf(node* t) {
        return leaf_set.count(t) ? (leaf_node*)t : 0;
    }

    node* bottom(uint);

    leaf_node* pred(node *, uint);
    leaf_node* succ(node*, uint);

    inline leaf_node* first() {
        return leaf_list;
    }

    inline leaf_node* last() {
        return leaf_list ? (leaf_node*)leaf_list->l : 0;
    }

    void insert_after(node*, leaf_node*);

    void add_checked(uint, char);

    void remove_leaf(leaf_node*);

    void copy(x_fast_trie& rhs) {
        clear();
        width = rhs.width;
        table.resize((size_t)width);
        for(auto x : rhs)
            insert(x);
    }

public:

    x_fast_trie(uint width = 32);

    x_fast_trie(const x_fast_trie& rhs) {
        copy(const_cast<x_fast_trie&>(rhs));
    }

    x_fast_trie& operator= (const x_fast_trie& rhs) {
        if (this == &rhs) return *this;
        copy(const_cast<x_fast_trie&>(rhs));

        return *this;
    }

    ~x_fast_trie() { clear(); }

    int size() const { return cnt; }

    void clear() {
        for(int i = 0; i < table.size(); ++i) {
            for(auto it = table[i].begin(); it != table[i].end(); ++it)
                delete (*it).second;
        }
        table.clear();
        leaf_set.clear();

        leaf_list = 0;
        cnt = 0;
    }

    int count(uint key) {
        return find(key) != end();
    }

    class iterator {
        x_fast_trie* t;
        leaf_node* it;

    public:
        friend class x_fast_trie;

        iterator(x_fast_trie* trie = 0, leaf_node* leaf = 0) : t(trie), it(leaf) {
        }

        iterator& operator = (const iterator& it) {
            if (this == &it) return *this;

            this->t  = it.t;
            this->it = it.it;

            return *this;
        }

        const uint& operator* () {
            return it->key;
        }

        iterator& operator++ () {
            if (it == t->last()) {
                it = 0;
            }
            else {
                it = (leaf_node*)it->r;
            }
            return *this;
        }

        iterator& operator--() {
            if (!it) {
                it = t->last();
            }
            else {
                it = (leaf_node*)it->l;
            }
            return *this;
        }

        bool operator== (const iterator& rhs) { return it == rhs.it; }
        bool operator!= (const iterator& rhs) { return it != rhs.it; }
    };
    friend class iterator;

    iterator begin() { return iterator(this, first()); }
    iterator end() { return iterator(this); }

    iterator insert(uint);

    iterator find(uint key) {
//        auto tmp = table[width-1][x];
//        return tmp ? iterator(this, (leaf_node*)tmp) : end();

        if (table[width-1].find(key >> 1) != table[width-1].end()) {
            node* t  = table[width - 1][key >> 1];
            if ((key & 1) == 1) {
                leaf_node* right = (leaf_node*)t->r;
                if (right && right->key == key) return iterator(this, right);
            }
            else {
                leaf_node* left = (leaf_node*)t->l;
                if(left && left->key == key) return iterator(this, left);
            }
        }

        return end();
    }

    iterator erase(uint);
    iterator erase(iterator);

    iterator succ(uint);
    iterator pred(uint);

    iterator lower_bound(uint);
};
}
}


#endif //PYSTON_X_FAST_TRIE_H
