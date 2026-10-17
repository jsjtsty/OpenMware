// A cache of shared, immutable-by-key resources (decoded pictures, decoded sound) under a byte budget.
//
// Two tiers keep the cost low without ever invalidating a pointer someone still uses:
//  - the index holds a weak_ptr per key, so a resource that is still referenced anywhere (a sprite, a playing voice) is found
//    again and never loaded twice, whatever the budget says;
//  - a strong reference list in least-recently-used order keeps recent resources alive while idle, up to `budget` bytes.
// Evicting only drops the cache's own reference: a resource in use lives on in its users, and is freed with the last one.
#pragma once

#include <algorithm>
#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace omw {

template <class T>
class SharedCache {
public:
	explicit SharedCache(size_t budget_bytes) : budget_(budget_bytes) {}
	SharedCache(const SharedCache&) = delete;
	SharedCache& operator=(const SharedCache&) = delete;

	// The live resource for `key` (and marks it most recently used), or null when it is not loaded.
	std::shared_ptr<T> find(const std::string& key) {
		const auto it = index_.find(key);
		if (it == index_.end()) return nullptr;
		Entry& e = it->second;
		std::shared_ptr<T> p = e.weak.lock();
		if (!p) {
			index_.erase(it);
			return nullptr;
		}
		retain(key, e, p);
		return p;
	}

	// Adds a resource of `bytes` bytes. `keep` = false registers it for sharing only (no idle retention), e.g. for very large items.
	void insert(const std::string& key, const std::shared_ptr<T>& p, size_t bytes, bool keep = true) {
		Entry& e = index_[key];
		unretain(e);
		e.weak = p;
		e.bytes = bytes;
		if (keep) retain(key, e, p);
		trim();
		if (index_.size() > prune_at_) prune();
	}

	// The size of an entry changed (reloaded in place).
	void resize(const std::string& key, size_t bytes) {
		const auto it = index_.find(key);
		if (it == index_.end()) return;
		if (it->second.retained) retained_bytes_ += bytes - it->second.bytes;
		it->second.bytes = bytes;
		trim();
	}

	// Drops the idle reference to one key (users keep what they hold).
	void drop(const std::string& key) {
		const auto it = index_.find(key);
		if (it == index_.end()) return;
		unretain(it->second);
		if (it->second.weak.expired()) index_.erase(it);
	}

	// Calls f(shared_ptr) for every live resource.
	template <class F>
	void for_each_live(F&& f) {
		for (auto& [k, e] : index_)
			if (auto p = e.weak.lock()) f(p);
	}

	void clear() {
		lru_.clear();
		index_.clear();
		retained_bytes_ = 0;
	}

	void set_budget(size_t bytes) {
		budget_ = bytes;
		trim();
	}
	size_t retained_bytes() const { return retained_bytes_; }
	size_t retained_count() const { return lru_.size(); }
	size_t indexed_count() const { return index_.size(); }

private:
	struct Node {
		std::string key;
		std::shared_ptr<T> ptr;
	};
	struct Entry {
		std::weak_ptr<T> weak;
		size_t bytes = 0;
		bool retained = false;
		typename std::list<Node>::iterator pos;
	};

	void retain(const std::string& key, Entry& e, const std::shared_ptr<T>& p) {
		if (e.retained) {
			lru_.splice(lru_.begin(), lru_, e.pos); // already idle-retained: move to the front
			return;
		}
		lru_.push_front({key, p});
		e.pos = lru_.begin();
		e.retained = true;
		retained_bytes_ += e.bytes;
	}

	void unretain(Entry& e) {
		if (!e.retained) return;
		retained_bytes_ -= e.bytes;
		lru_.erase(e.pos);
		e.retained = false;
	}

	// Releases the least recently used references until the budget holds; the newest entry always stays.
	void trim() {
		while (retained_bytes_ > budget_ && lru_.size() > 1) {
			const auto it = index_.find(lru_.back().key);
			lru_.pop_back();
			if (it == index_.end()) continue;
			it->second.retained = false;
			retained_bytes_ -= it->second.bytes;
			if (it->second.weak.expired()) index_.erase(it); // nobody else held it: gone for good
		}
	}

	// Entries of resources that were freed by their last user leave only a stale key behind; sweep them occasionally.
	void prune() {
		for (auto it = index_.begin(); it != index_.end();) {
			if (!it->second.retained && it->second.weak.expired()) it = index_.erase(it);
			else ++it;
		}
		prune_at_ = std::max<size_t>(1024, index_.size() * 2);
	}

	size_t budget_;
	size_t retained_bytes_ = 0;
	size_t prune_at_ = 1024;
	std::list<Node> lru_; // front = most recently used
	std::unordered_map<std::string, Entry> index_;
};

} // namespace omw
