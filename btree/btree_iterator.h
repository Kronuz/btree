/*
 * Copyright (c) 2019-2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2013 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * btree_iterator: the iterator over node slots, with its member definitions.
 * A real header, but the pieces are meant to be used through btree/btree.h.
 */

// btree_iterator: the iterator over node slots.

#ifndef BTREE_BTREE_ITERATOR_H__
#define BTREE_BTREE_ITERATOR_H__

#include <assert.h>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace btree {

template <typename Node, typename Reference, typename Pointer>
class btree_iterator {
public:
	typedef typename Node::key_type key_type;
	typedef typename Node::size_type size_type;
	typedef typename Node::difference_type difference_type;
	typedef typename Node::params_type params_type;

	typedef Node node_type;
	typedef typename std::remove_const<Node>::type normal_node;
	typedef const Node const_node;
	typedef typename params_type::value_type value_type;
	typedef typename params_type::pointer normal_pointer;
	typedef typename params_type::reference normal_reference;
	typedef typename params_type::const_pointer const_pointer;
	typedef typename params_type::const_reference const_reference;

	typedef Pointer pointer;
	typedef Reference reference;
	typedef std::bidirectional_iterator_tag iterator_category;

	typedef btree_iterator<normal_node, normal_reference, normal_pointer> iterator;
	typedef btree_iterator<const_node, const_reference, const_pointer> const_iterator;
	typedef btree_iterator<Node, Reference, Pointer> self_type;

	btree_iterator()
		: node(nullptr),
		  position(-1) {
	}
	btree_iterator(const iterator& x)
		: node(x.node),
		  position(x.position) {
	}

	bool operator==(const const_iterator& x) const {
		return node == x.node && position == x.position;
	}
	bool operator!=(const const_iterator& x) const {
		return node != x.node || position != x.position;
	}
	bool operator==(const iterator& x) const {
		return node == x.node && position == x.position;
	}
	bool operator!=(const iterator& x) const {
		return node != x.node || position != x.position;
	}

	// Accessors for the key/value the iterator is pointing at.
	const key_type& key() const {
		return node->key(position);
	}
	reference operator*() const {
		return node->value(position);
	}
	pointer operator->() const {
		return &node->value(position);
	}

	self_type& operator++() {
		increment();
		return *this;
	}
	self_type& operator--() {
		decrement();
		return *this;
	}
	self_type operator++(int) {
		self_type tmp =* this;
		++*this;
		return tmp;
	}
	self_type operator--(int) {
		self_type tmp =* this;
		--*this;
		return tmp;
	}

private:
	btree_iterator(Node* n, int p)
		: node(const_cast<normal_node*>(n)),
		  position(p) {
	}

	// Increment/decrement the iterator.
	void increment() {
		if (node->is_leaf() && ++position < node->count()) {
			return;
		}
		increment_slow();
	}
	void increment_by(int count);
	void increment_slow();

	void decrement() {
		if (node->is_leaf() && --position >= 0) {
			return;
		}
		decrement_slow();
	}
	void decrement_slow();

	// The node in the tree the iterator is pointing at.
	normal_node* node;
	// The position within the node of the tree the iterator is pointing at.
	int position;

	template <typename> friend class btree;
	friend iterator;
	friend const_iterator;
};

////
// btree_iterator methods
template <typename N, typename R, typename P>
void btree_iterator<N, R, P>::increment_slow() {
	if (node->is_leaf()) {
		assert(position >= node->count());
		self_type save(*this);
		while (position == node->count() && !node->is_root()) {
			assert(node->get_parent()->child(node->position()) == node);
			position = node->position();
			node = node->get_parent();
		}
		if (position == node->count()) {
			*this = save;
		}
	} else {
		assert(position < node->count());
		node = node->child(position + 1);
		while (!node->is_leaf()) {
			node = node->child(0);
		}
		position = 0;
	}
}

template <typename N, typename R, typename P>
void btree_iterator<N, R, P>::increment_by(int count) {
	while (count > 0) {
		if (node->is_leaf()) {
			int rest = node->count() - position;
			position += std::min(rest, count);
			count = count - rest;
			if (position < node->count()) {
				return;
			}
		} else {
			--count;
		}
		increment_slow();
	}
}

template <typename N, typename R, typename P>
void btree_iterator<N, R, P>::decrement_slow() {
	if (node->is_leaf()) {
		assert(position <= -1);
		self_type save(*this);
		while (position < 0 && !node->is_root()) {
			assert(node->get_parent()->child(node->position()) == node);
			position = node->position() - 1;
			node = node->get_parent();
		}
		if (position < 0) {
			*this = save;
		}
	} else {
		assert(position >= 0);
		node = node->child(position);
		while (!node->is_leaf()) {
			node = node->child(node->count());
		}
		position = node->count() - 1;
	}
}

} // namespace btree

#endif  // BTREE_BTREE_ITERATOR_H__
