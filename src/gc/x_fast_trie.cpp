//
// Created by user on 8/25/15.
//

#include "x_fast_trie.h"

namespace pyston {
namespace gc {

x_fast_trie::x_fast_trie(uint width) : width(width) {
    leaf_list = 0;
    cnt = 0;
    table.resize(width);
}

x_fast_trie::node * x_fast_trie::bottom(uint key) {
    int l = 0;
    int h = width;
    node* b = 0;

    do {
        int m = (l + h) >> 1;
        uint ancestor = key >> (width - 1 - m) >> 1;

        if (table[m].find(ancestor) != table[m].end()) {
            node* tmp = table[m][ancestor];
            l = m + 1;
            b = tmp;
        }
        else {
            h = m;
        }
    } while (l < h);

    return b;
}

x_fast_trie::leaf_node * x_fast_trie::pred(x_fast_trie::node *bottom, uint key) {
    if (!bottom) return 0;

    leaf_node* l = as_leaf(bottom->r);
    if (l && l->key < key) return l;

    l = as_leaf(bottom->l);
    if (l && l->key < key) return l;

    l = as_leaf(bottom->l->l);
    if (l && l->key < key) return l;

    return 0;
}

x_fast_trie::leaf_node * x_fast_trie::succ(x_fast_trie::node* bottom, uint key) {
    if (!bottom) return 0;

    leaf_node* l = as_leaf(bottom->l);
    if (l && l->key > key) return l;

    l = as_leaf(bottom->r);
    if (l && l->key > key) return l;

    l = as_leaf(bottom->r->r);
    if (l && l->key > key) return l;

    return 0;
}

x_fast_trie::iterator x_fast_trie::insert(uint x) {
    add_checked(x, false);
    return find(x);
}

x_fast_trie::iterator x_fast_trie::erase(uint key) {
    auto b = bottom(key);
    if (!b) return end();

    leaf_node* end_node = as_leaf(b->l);
    if (!end_node || end_node->key != key) {
        end_node = as_leaf(b->r);
        if (!end_node || end_node->key != key)
            return end();
    }

    return erase(iterator(this, end_node));
}

x_fast_trie::iterator x_fast_trie::erase(x_fast_trie::iterator it) {
    if (it == end()) return it;

    leaf_node* end_node = it.it;
    uint key = end_node->key;

    node* left_leaf = end_node->l;
    node* right_leaf = end_node->r;

    remove_leaf(end_node);
    // iterate levels
    bool single = true;
    for(int i = width - 1; i >= 0; --i) {
        uint id = key >> (width - 1 - i) >> 1;
        bool isFromRight = ((key >> (width - 1 - i)) & 1) == 1;
        node* cur = table[i][id];
        // remove node
        if (single) {
            if (isFromRight &&
                ( !leaf_set.count(cur->l) ||
                  (i == (width - 1) && static_cast<leaf_node*>(cur->l)->key != key) ))
            {
                cur->r = left_leaf;
                single = false;
            }
            else if (!isFromRight && ( !leaf_set.count(cur->r) || (i == (width - 1) &&
                                                                   reinterpret_cast<leaf_node*>(cur->r)->key != key)))
            {
                cur->l = right_leaf;
                single = false;
            }
            else {
                table[i].erase(id);
            }
        }
        else {
            if (cur->l == end_node)
                cur->l = right_leaf;
            else if (cur->r == end_node)
                cur->r = left_leaf;
        }
    }
    cnt--;

    bool f = right_leaf && static_cast<leaf_node*>(right_leaf)->key <= key;
    leaf_set.erase(end_node);
    delete end_node;

    return f ? end() : iterator(this, (leaf_node *) right_leaf);
}

x_fast_trie::iterator x_fast_trie::succ(uint x) {
    leaf_node* tmp = succ(bottom(x), x);
    return tmp ? x_fast_trie::iterator(this, tmp) : end();
}

x_fast_trie::iterator x_fast_trie::pred(uint x) {
    leaf_node* tmp = pred(bottom(x), x);
    return tmp ? x_fast_trie::iterator(this, tmp) : end();
}

x_fast_trie::iterator x_fast_trie::lower_bound(uint x) {
    auto it = find(x);
    return it == end() ? succ(x) : it;
//    if (it == end()) {
//        it = pred(x);
//        while (it != end() && *it < x) ++it;
//    }
//    return it;
}

void x_fast_trie::add_checked(uint key, char overwrite) {
    node* b = bottom(key);
    leaf_node* predecessor = pred(b, key);
    leaf_node* pred_right = predecessor ? static_cast<leaf_node*>(predecessor->r): leaf_list;

    if (pred_right && pred_right->key == key) {
        assert(overwrite && "overwrite");
        return;
    }

    cnt++;

    leaf_node* end_node = new leaf_node(key);
    leaf_set.insert((void*)end_node);
    insert_after(predecessor, end_node);
    // fix the jump path
    if (!b) {
        b = new node();
        table[0][0] = b;
        b->l = end_node;
        b->r = end_node;
    }

    node* old_node = 0;
    node* cur;
    for (int i = 0; i < width; ++i) {
        uint id = key >> (width - 1 - i) >> 1;

        if (table[i].find(id) != table[i].end()) {
            cur = table[i][id];
            // fix the jump path
            leaf_node* leaf = as_leaf(cur->l);
            if (leaf && leaf->key > key) cur->l = end_node;
            else {
                leaf = as_leaf(cur->r);
                if (leaf && leaf->key < key)
                    cur->r = end_node;
            }
        }
        else {
            // insert new node
            cur = new node(end_node, end_node);
            table[i][id] = cur;
            // fix link between old and new node
            if ((id & 1) > 0)
                old_node->r = cur;
            else
                old_node->l = cur;
        }
        old_node = cur;
    }
}

void x_fast_trie::insert_after(x_fast_trie::node *marker, x_fast_trie::leaf_node *new_leaf) {
    if (!marker) {
        if (!leaf_list) {
            leaf_list = new_leaf;
            new_leaf->l = new_leaf;
            new_leaf->r = new_leaf;
        }
        else {
            leaf_list->l->r = new_leaf;
            new_leaf->l = leaf_list->l;
            new_leaf->r = leaf_list;
            leaf_list->l = new_leaf;
            leaf_list = new_leaf;
        }
    }
    else {
        node* right_node = marker->r;
        marker->r = new_leaf;
        new_leaf->l = marker;
        new_leaf->r = right_node;
        right_node->l = new_leaf;
    }
}

void x_fast_trie::remove_leaf(x_fast_trie::leaf_node *leaf) {
    leaf_node* right = (leaf_node*)leaf->r;
    if (right == leaf) {
        leaf_list = 0;
    }
    else {
        leaf->l->r = right;
        right->l = leaf->l;
        if (leaf == leaf_list)
            leaf_list = right;
    }
}
}
}