#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <functional>

namespace yob {

enum class Color : uint8_t { Red = 0, Black = 1 };

struct RBTreeNodeBase {
    RBTreeNodeBase* parent = nullptr;
    RBTreeNodeBase* left = nullptr;
    RBTreeNodeBase* right = nullptr;
    Color color = Color::Red;

    [[nodiscard]] bool is_red() const noexcept { return color == Color::Red; }
    [[nodiscard]] bool is_black() const noexcept { return color == Color::Black; }
};

template<typename T>
struct IntrusiveHook {
    static RBTreeNodeBase* to_node(T* obj) noexcept;
    static T* from_node(RBTreeNodeBase* node) noexcept;
    static const T* from_node(const RBTreeNodeBase* node) noexcept;
};

template<
    typename T,
    typename KeyOfValue,
    typename Compare = std::less<typename KeyOfValue::type>,
    typename Hook = IntrusiveHook<T>
>
class IntrusiveRBTree {
public:
    using key_type = typename KeyOfValue::type;
    using value_type = T;
    using node_type = RBTreeNodeBase;
    using size_type = size_t;

    IntrusiveRBTree() = default;
    ~IntrusiveRBTree() { clear(); }

    IntrusiveRBTree(const IntrusiveRBTree&) = delete;
    IntrusiveRBTree& operator=(const IntrusiveRBTree&) = delete;
    IntrusiveRBTree(IntrusiveRBTree&&) = delete;
    IntrusiveRBTree& operator=(IntrusiveRBTree&&) = delete;

    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }
    [[nodiscard]] size_type size() const noexcept { return size_; }

    [[nodiscard]] bool insert(T* value) noexcept {
        node_type* node = Hook::to_node(value);
        const key_type key = KeyOfValue{}(*value);

        node_type* parent = nullptr;
        node_type* current = root_;

        while (current != nullptr) {
            parent = current;
            const key_type current_key = KeyOfValue{}(*Hook::from_node(current));

            if (Compare{}(key, current_key)) {
                current = current->left;
            } else if (Compare{}(current_key, key)) {
                current = current->right;
            } else {
                return false;
            }
        }

        node->parent = parent;
        node->left = nullptr;
        node->right = nullptr;
        node->color = Color::Red;

        if (parent == nullptr) {
            root_ = node;
        } else if (Compare{}(key, KeyOfValue{}(*Hook::from_node(parent)))) {
            parent->left = node;
        } else {
            parent->right = node;
        }

        ++size_;
        insert_fixup(node);
        return true;
    }

    [[nodiscard]] T* find(const key_type& key) noexcept {
        node_type* node = find_node(key);
        return node ? Hook::from_node(node) : nullptr;
    }

    [[nodiscard]] const T* find(const key_type& key) const noexcept {
        const node_type* node = find_node(key);
        return node ? Hook::from_node(node) : nullptr;
    }

    void remove(T* value) noexcept {
        node_type* z = Hook::to_node(value);
        node_type* y = z;
        node_type* x = nullptr;
        Color y_original_color = y->color;

        if (z->left == nullptr) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == nullptr) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y = minimum_node(z->right);
            y_original_color = y->color;
            x = y->right;

            if (y->parent == z) {
                if (x) x->parent = y;
            } else {
                transplant(y, y->right);
                y->right = z->right;
                y->right->parent = y;
            }

            transplant(z, y);
            y->left = z->left;
            y->left->parent = y;
            y->color = z->color;
        }

        z->parent = nullptr;
        z->left = nullptr;
        z->right = nullptr;
        z->color = Color::Red;

        --size_;

        if (y_original_color == Color::Black && x != nullptr) {
            delete_fixup(x);
        }

        if (root_ != nullptr) {
            root_->color = Color::Black;
        }
    }

    [[nodiscard]] T* minimum() noexcept {
        return root_ ? Hook::from_node(minimum_node(root_)) : nullptr;
    }

    [[nodiscard]] const T* minimum() const noexcept {
        return root_ ? Hook::from_node(minimum_node(root_)) : nullptr;
    }

    [[nodiscard]] T* maximum() noexcept {
        return root_ ? Hook::from_node(maximum_node(root_)) : nullptr;
    }

    [[nodiscard]] const T* maximum() const noexcept {
        return root_ ? Hook::from_node(maximum_node(root_)) : nullptr;
    }

    [[nodiscard]] T* predecessor(T* value) noexcept {
        node_type* node = Hook::to_node(value);
        if (node->left != nullptr) {
            return Hook::from_node(maximum_node(node->left));
        }
        node_type* parent = node->parent;
        while (parent != nullptr && node == parent->left) {
            node = parent;
            parent = parent->parent;
        }
        return parent ? Hook::from_node(parent) : nullptr;
    }

    [[nodiscard]] const T* predecessor(const T* value) const noexcept {
        const node_type* node = Hook::to_node(const_cast<T*>(value));
        if (node->left != nullptr) {
            return Hook::from_node(maximum_node(node->left));
        }
        const node_type* parent = node->parent;
        while (parent != nullptr && node == parent->left) {
            node = parent;
            parent = parent->parent;
        }
        return parent ? Hook::from_node(parent) : nullptr;
    }

    [[nodiscard]] T* successor(T* value) noexcept {
        node_type* node = Hook::to_node(value);
        if (node->right != nullptr) {
            return Hook::from_node(minimum_node(node->right));
        }
        node_type* parent = node->parent;
        while (parent != nullptr && node == parent->right) {
            node = parent;
            parent = parent->parent;
        }
        return parent ? Hook::from_node(parent) : nullptr;
    }

    [[nodiscard]] const T* successor(const T* value) const noexcept {
        const node_type* node = Hook::to_node(const_cast<T*>(value));
        if (node->right != nullptr) {
            return Hook::from_node(minimum_node(node->right));
        }
        const node_type* parent = node->parent;
        while (parent != nullptr && node == parent->right) {
            node = parent;
            parent = parent->parent;
        }
        return parent ? Hook::from_node(parent) : nullptr;
    }

    void clear() noexcept {
        root_ = nullptr;
        size_ = 0;
    }

    template<typename Func>
    void inorder(Func&& f) const {
        inorder_traversal(root_, std::forward<Func>(f));
    }

private:
    node_type* root_ = nullptr;
    size_type size_ = 0;

    void rotate_left(node_type* x) noexcept {
        node_type* y = x->right;
        x->right = y->left;
        if (y->left != nullptr) {
            y->left->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nullptr) {
            root_ = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    void rotate_right(node_type* y) noexcept {
        node_type* x = y->left;
        y->left = x->right;
        if (x->right != nullptr) {
            x->right->parent = y;
        }
        x->parent = y->parent;
        if (y->parent == nullptr) {
            root_ = x;
        } else if (y == y->parent->right) {
            y->parent->right = x;
        } else {
            y->parent->left = x;
        }
        x->right = y;
        y->parent = x;
    }

    void insert_fixup(node_type* z) noexcept {
        while (z->parent != nullptr && z->parent->is_red()) {
            if (z->parent == z->parent->parent->left) {
                node_type* y = z->parent->parent->right;
                if (y != nullptr && y->is_red()) {
                    z->parent->color = Color::Black;
                    y->color = Color::Black;
                    z->parent->parent->color = Color::Red;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        z = z->parent;
                        rotate_left(z);
                    }
                    z->parent->color = Color::Black;
                    z->parent->parent->color = Color::Red;
                    rotate_right(z->parent->parent);
                }
            } else {
                node_type* y = z->parent->parent->left;
                if (y != nullptr && y->is_red()) {
                    z->parent->color = Color::Black;
                    y->color = Color::Black;
                    z->parent->parent->color = Color::Red;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rotate_right(z);
                    }
                    z->parent->color = Color::Black;
                    z->parent->parent->color = Color::Red;
                    rotate_left(z->parent->parent);
                }
            }
        }
        root_->color = Color::Black;
    }

    void delete_fixup(node_type* x) noexcept {
        while (x != root_ && x->is_black()) {
            if (x == x->parent->left) {
                node_type* w = x->parent->right;
                if (w->is_red()) {
                    w->color = Color::Black;
                    x->parent->color = Color::Red;
                    rotate_left(x->parent);
                    w = x->parent->right;
                }
                if ((w->left == nullptr || w->left->is_black()) &&
                    (w->right == nullptr || w->right->is_black())) {
                    w->color = Color::Red;
                    x = x->parent;
                } else {
                    if (w->right == nullptr || w->right->is_black()) {
                        if (w->left != nullptr) w->left->color = Color::Black;
                        w->color = Color::Red;
                        rotate_right(w);
                        w = x->parent->right;
                    }
                    w->color = x->parent->color;
                    x->parent->color = Color::Black;
                    if (w->right != nullptr) w->right->color = Color::Black;
                    rotate_left(x->parent);
                    x = root_;
                }
            } else {
                node_type* w = x->parent->left;
                if (w->is_red()) {
                    w->color = Color::Black;
                    x->parent->color = Color::Red;
                    rotate_right(x->parent);
                    w = x->parent->left;
                }
                if ((w->right == nullptr || w->right->is_black()) &&
                    (w->left == nullptr || w->left->is_black())) {
                    w->color = Color::Red;
                    x = x->parent;
                } else {
                    if (w->left == nullptr || w->left->is_black()) {
                        if (w->right != nullptr) w->right->color = Color::Black;
                        w->color = Color::Red;
                        rotate_left(w);
                        w = x->parent->left;
                    }
                    w->color = x->parent->color;
                    x->parent->color = Color::Black;
                    if (w->left != nullptr) w->left->color = Color::Black;
                    rotate_right(x->parent);
                    x = root_;
                }
            }
        }
        x->color = Color::Black;
    }

    void transplant(node_type* u, node_type* v) noexcept {
        if (u->parent == nullptr) {
            root_ = v;
        } else if (u == u->parent->left) {
            u->parent->left = v;
        } else {
            u->parent->right = v;
        }
        if (v != nullptr) {
            v->parent = u->parent;
        }
    }

    [[nodiscard]] node_type* minimum_node(node_type* node) const noexcept {
        while (node->left != nullptr) {
            node = node->left;
        }
        return node;
    }

    [[nodiscard]] node_type* maximum_node(node_type* node) const noexcept {
        while (node->right != nullptr) {
            node = node->right;
        }
        return node;
    }

    [[nodiscard]] node_type* find_node(const key_type& key) const noexcept {
        node_type* current = root_;
        while (current != nullptr) {
            const key_type current_key = KeyOfValue{}(*Hook::from_node(current));
            if (Compare{}(key, current_key)) {
                current = current->left;
            } else if (Compare{}(current_key, key)) {
                current = current->right;
            } else {
                return current;
            }
        }
        return nullptr;
    }

    template<typename Func>
    void inorder_traversal(node_type* node, Func&& f) const {
        if (node == nullptr) return;
        inorder_traversal(node->left, f);
        f(*Hook::from_node(node));
        inorder_traversal(node->right, f);
    }
};

} // namespace yob
