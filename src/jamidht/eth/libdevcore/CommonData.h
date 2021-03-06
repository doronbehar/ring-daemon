/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file CommonData.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * Shared algorithms and data types.
 */

#pragma once

#include <vector>
#include <algorithm>
#include <unordered_set>
#include <type_traits>
#include <cstring>
#include <string>
#include "Common.h"

namespace dev
{

// String conversion functions, mainly to/from hex/nibble/byte representations.

enum class WhenError
{
	DontThrow = 0,
	Throw = 1,
};

template <class Iterator>
std::string toHex(Iterator _it, Iterator _end, std::string _prefix)
{
	typedef std::iterator_traits<Iterator> traits;
	static_assert(sizeof(typename traits::value_type) == 1, "toHex needs byte-sized element type");

	static char const* hexdigits = "0123456789abcdef";
	size_t off = _prefix.size();
	std::string hex(std::distance(_it, _end)*2 + off, '0');
	hex.replace(0, off, _prefix);
	for (; _it != _end; _it++)
	{
		hex[off++] = hexdigits[(*_it >> 4) & 0x0f];
		hex[off++] = hexdigits[*_it & 0x0f];
	}
	return hex;
}

/// Convert a series of bytes to the corresponding hex string.
/// @example toHex("A\x69") == "4169"
template <class T> std::string toHex(T const& _data)
{
	return toHex(_data.begin(), _data.end(), "");
}

/// Convert a series of bytes to the corresponding hex string with 0x prefix.
/// @example toHexPrefixed("A\x69") == "0x4169"
template <class T> std::string toHexPrefixed(T const& _data)
{
	return toHex(_data.begin(), _data.end(), "0x");
}

/// Converts a (printable) ASCII hex string into the corresponding byte stream.
/// @example fromHex("41626261") == asBytes("Abba")
/// If _throw = ThrowType::DontThrow, it replaces bad hex characters with 0's, otherwise it will throw an exception.
bytes fromHex(std::string const& _s, WhenError _throw = WhenError::DontThrow);

/// @returns true if @a _s is a hex string.
bool isHex(std::string const& _s) noexcept;

/// @returns true if @a _hash is a hash conforming to FixedHash type @a T.
template <class T> static bool isHash(std::string const& _hash)
{
	return (_hash.size() == T::size * 2 || (_hash.size() == T::size * 2 + 2 && _hash.substr(0, 2) == "0x")) && isHex(_hash);
}

/// Converts byte array to a string containing the same (binary) data. Unless
/// the byte array happens to contain ASCII data, this won't be printable.
inline std::string asString(bytes const& _b)
{
	return std::string((char const*)_b.data(), (char const*)(_b.data() + _b.size()));
}

/// Converts byte array ref to a string containing the same (binary) data. Unless
/// the byte array happens to contain ASCII data, this won't be printable.
inline std::string asString(bytesConstRef _b)
{
	return std::string((char const*)_b.data(), (char const*)(_b.data() + _b.size()));
}

/// Converts a string to a byte array containing the string's (byte) data.
inline bytes asBytes(std::string const& _b)
{
	return bytes((uint8_t const*)_b.data(), (uint8_t const*)(_b.data() + _b.size()));
}

/// Converts a string into the big-endian base-16 stream of integers (NOT ASCII).
/// @example asNibbles("A")[0] == 4 && asNibbles("A")[1] == 1
bytes asNibbles(bytesConstRef const& _s);


// Big-endian to/from host endian conversion functions.

/// Converts a big-endian byte-stream represented on a templated collection to a templated integer value.
/// @a _In will typically be either std::string or bytes.
/// @a T will typically by unsigned, u160, u256 or bigint.
template <class T, class _In>
inline T fromBigEndian(_In const& _bytes)
{
	T ret = (T)0;
	for (auto i: _bytes)
		ret = (T)((ret << 8) | (uint8_t)(typename std::make_unsigned<decltype(i)>::type)i);
	return ret;
}

inline bytes toCompactBigEndian(uint8_t _val, unsigned _min = 0)
{
	return (_min || _val) ? bytes{ _val } : bytes{};
}

// Algorithms for string and string-like collections.

/// Determines the length of the common prefix of the two collections given.
/// @returns the number of elements both @a _t and @a _u share, in order, at the beginning.
/// @example commonPrefix("Hello world!", "Hello, world!") == 5
template <class T, class _U>
unsigned commonPrefix(T const& _t, _U const& _u)
{
	unsigned s = std::min<unsigned>(_t.size(), _u.size());
	for (unsigned i = 0;; ++i)
		if (i == s || _t[i] != _u[i])
			return i;
	return s;
}

/// Trims a given number of elements from the front of a collection.
/// Only works for POD element types.
template <class T>
void trimFront(T& _t, unsigned _elements)
{
	static_assert(std::is_pod<typename T::value_type>::value, "");
	memmove(_t.data(), _t.data() + _elements, (_t.size() - _elements) * sizeof(_t[0]));
	_t.resize(_t.size() - _elements);
}

/// Pushes an element on to the front of a collection.
/// Only works for POD element types.
template <class T, class _U>
void pushFront(T& _t, _U _e)
{
	static_assert(std::is_pod<typename T::value_type>::value, "");
	_t.push_back(_e);
	memmove(_t.data() + 1, _t.data(), (_t.size() - 1) * sizeof(_e));
	_t[0] = _e;
}

/// Concatenate two vectors of elements of POD types.
template <class T>
inline std::vector<T>& operator+=(std::vector<typename std::enable_if<std::is_pod<T>::value, T>::type>& _a, std::vector<T> const& _b)
{
	auto s = _a.size();
	_a.resize(_a.size() + _b.size());
	memcpy(_a.data() + s, _b.data(), _b.size() * sizeof(T));
	return _a;

}

/// Concatenate two vectors of elements.
template <class T>
inline std::vector<T>& operator+=(std::vector<typename std::enable_if<!std::is_pod<T>::value, T>::type>& _a, std::vector<T> const& _b)
{
	_a.reserve(_a.size() + _b.size());
	for (auto& i: _b)
		_a.push_back(i);
	return _a;
}

/// Insert the contents of a container into a set
template <class T, class U> std::set<T>& operator+=(std::set<T>& _a, U const& _b)
{
	for (auto const& i: _b)
		_a.insert(i);
	return _a;
}

/// Insert the contents of a container into an unordered_set
template <class T, class U> std::unordered_set<T>& operator+=(std::unordered_set<T>& _a, U const& _b)
{
	for (auto const& i: _b)
		_a.insert(i);
	return _a;
}

/// Concatenate the contents of a container onto a vector
template <class T, class U> std::vector<T>& operator+=(std::vector<T>& _a, U const& _b)
{
	for (auto const& i: _b)
		_a.push_back(i);
	return _a;
}

/// Insert the contents of a container into a set
template <class T, class U> std::set<T> operator+(std::set<T> _a, U const& _b)
{
	return _a += _b;
}

/// Insert the contents of a container into an unordered_set
template <class T, class U> std::unordered_set<T> operator+(std::unordered_set<T> _a, U const& _b)
{
	return _a += _b;
}

/// Concatenate the contents of a container onto a vector
template <class T, class U> std::vector<T> operator+(std::vector<T> _a, U const& _b)
{
	return _a += _b;
}

/// Concatenate two vectors of elements.
template <class T>
inline std::vector<T> operator+(std::vector<T> const& _a, std::vector<T> const& _b)
{
	std::vector<T> ret(_a);
	return ret += _b;
}

template<class T, class U>
std::vector<T> keysOf(std::map<T, U> const& _m)
{
	std::vector<T> ret;
	for (auto const& i: _m)
		ret.push_back(i.first);
	return ret;
}

template<class T, class U>
std::vector<T> keysOf(std::unordered_map<T, U> const& _m)
{
	std::vector<T> ret;
	for (auto const& i: _m)
		ret.push_back(i.first);
	return ret;
}

template<class T, class U>
std::vector<U> valuesOf(std::map<T, U> const& _m)
{
	std::vector<U> ret;
	ret.reserve(_m.size());
	for (auto const& i: _m)
		ret.push_back(i.second);
	return ret;
}

template<class T, class U>
std::vector<U> valuesOf(std::unordered_map<T, U> const& _m)
{
	std::vector<U> ret;
	ret.reserve(_m.size());
	for (auto const& i: _m)
		ret.push_back(i.second);
	return ret;
}

template <class T, class V>
bool contains(T const& _t, V const& _v)
{
	return std::end(_t) != std::find(std::begin(_t), std::end(_t), _v);
}

}
