#pragma once

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

// Minimal vector-backed adapter for the historical F4SE VMArray call sites.
// Papyrus type integration remains in LegacyCompat.h; keeping this container
// independent lets its row-preservation behavior run as a host test.
template <class T>
class VMArray
{
public:
	using value_type = T;
	using size_type = typename std::vector<T>::size_type;
	using iterator = typename std::vector<T>::iterator;
	using const_iterator = typename std::vector<T>::const_iterator;

	VMArray() = default;
	VMArray(std::initializer_list<T> a_values) : _values(a_values) {}

	[[nodiscard]] std::uint32_t Length() const noexcept { return static_cast<std::uint32_t>(_values.size()); }
	[[nodiscard]] size_type size() const noexcept { return _values.size(); }
	[[nodiscard]] bool empty() const noexcept { return _values.empty(); }

	bool Get(T* a_out, std::uint32_t a_index) const
	{
		if (!a_out) {
			return false;
		}
		// The legacy wrapper left the destination untouched on failure. Most
		// inherited call sites read immediately after Get(), so a malformed or
		// concurrently changed array could otherwise expose an indeterminate
		// pointer. Preserve successful behavior while making failure null-safe.
		*a_out = T{};
		if (a_index >= _values.size()) {
			return false;
		}
		*a_out = _values[a_index];
		return true;
	}

	bool Set(const T* a_value, std::uint32_t a_index)
	{
		if (!a_value || a_index >= _values.size()) {
			return false;
		}
		_values[a_index] = *a_value;
		return true;
	}

	bool Push(const T* a_value)
	{
		if (!a_value) {
			return false;
		}
		_values.push_back(*a_value);
		return true;
	}

	void push_back(T&& a_value) { _values.push_back(std::move(a_value)); }
	void push_back(const T& a_value) { _values.push_back(a_value); }
	iterator begin() noexcept { return _values.begin(); }
	iterator end() noexcept { return _values.end(); }
	const_iterator begin() const noexcept { return _values.begin(); }
	const_iterator end() const noexcept { return _values.end(); }

	T& operator[](size_type a_index) { return _values[a_index]; }
	const T& operator[](size_type a_index) const { return _values[a_index]; }

private:
	std::vector<T> _values;
};
