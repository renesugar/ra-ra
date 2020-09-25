// -*- mode: c++; coding: utf-8 -*-
/// @file view-ops.hh
/// @brief Operations specific to Views

// (c) Daniel Llorens - 2013-2014, 2017
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option) any
// later version.

#pragma once
#include "ra/concrete.hh"
#include <complex>

namespace ra {

template <class T, rank_t RANK> inline
View<T, RANK> reverse(View<T, RANK> const & view, int k)
{
    View<T, RANK> r = view;
    auto & dim = r.dim[k];
    if (dim.size!=0) {
        r.p += dim.stride*(dim.size-1);
        dim.stride *= -1;
    }
    return r;
}

// dynamic transposed axes list.
template <class T, rank_t RANK, class S> inline
View<T, RANK_ANY> transpose_(S && s, View<T, RANK> const & view)
{
    RA_CHECK(view.rank()==dim_t(ra::size(s)));
    auto rp = std::max_element(s.begin(), s.end());
    rank_t dstrank = (rp==s.end() ? 0 : *rp+1);

    View<T, RANK_ANY> r { decltype(r.dim)(dstrank, Dim { DIM_BAD, 0 }), view.data() };
    for (int k=0; int sk: s) {
        Dim & dest = r.dim[sk];
        dest.stride += view.dim[k].stride;
        dest.size = dest.size>=0 ? std::min(dest.size, view.dim[k].size) : view.dim[k].size;
        ++k;
    }
    return r;
}

template <class T, rank_t RANK, class S> inline
View<T, RANK_ANY> transpose(S && s, View<T, RANK> const & view)
{
    return transpose_(std::forward<S>(s), view);
}

// Note that we need the compile time values and not the sizes to deduce the rank of the output, so it would be useless to provide a builtin array shim as we do with reshape().
template <class T, rank_t RANK> inline
View<T, RANK_ANY> transpose(std::initializer_list<ra::rank_t> s, View<T, RANK> const & view)
{
    return transpose_(s, view);
}

// static transposed axes list.
template <int ... Iarg, class T, rank_t RANK> inline
auto transpose(View<T, RANK> const & view)
{
    static_assert(RANK==RANK_ANY || RANK==sizeof...(Iarg), "bad output rank");
    RA_CHECK((view.rank()==sizeof...(Iarg)) && "bad output rank");

    using dummy_s = mp::makelist<sizeof...(Iarg), mp::int_t<0>>;
    using ti = axes_list_indices<mp::int_list<Iarg ...>, dummy_s, dummy_s>;
    constexpr rank_t DSTRANK = mp::len<typename ti::dst>;

    View<T, DSTRANK> r { decltype(r.dim)(Dim { DIM_BAD, 0 }), view.data() };
    std::array<int, sizeof...(Iarg)> s {{ Iarg ... }};
    for (int k=0; int sk: s) {
        Dim & dest = r.dim[sk];
        dest.stride += view.dim[k].stride;
        dest.size = dest.size>=0 ? std::min(dest.size, view.dim[k].size) : view.dim[k].size;
        ++k;
    }
    return r;
}

template <class T, rank_t RANK> inline
auto diag(View<T, RANK> const & view)
{
    return transpose<0, 0>(view);
}

template <class T, rank_t RANK> inline
bool is_ravel_free(View<T, RANK> const & a)
{
    int r = a.rank()-1;
    for (; r>=0 && a.size(r)==1; --r) {}
    if (r<0) { return true; }
    ra::dim_t s = a.stride(r)*a.size(r);
    while (--r>=0) {
        if (1!=a.size(r)) {
            if (a.stride(r)!=s) {
                return false;
            }
            s *= a.size(r);
        }
    }
    return true;
}

template <class T, rank_t RANK> inline
View<T, 1> ravel_free(View<T, RANK> const & a)
{
    RA_CHECK(is_ravel_free(a));
    int r = a.rank()-1;
    for (; r>=0 && a.size(r)==1; --r) {}
    ra::dim_t s = r<0 ? 1 : a.stride(r);
    return ra::View<T, 1>({{size(a), s}}, a.p);
}

template <class T, rank_t RANK, class S> inline
auto reshape_(View<T, RANK> const & a, S && sb_)
{
    auto sb = concrete(std::forward<S>(sb_));
// FIXME when we need to copy, accept/return Shared
    dim_t la = ra::size(a);
    dim_t lb = 1;
    for (int i=0; i<ra::size(sb); ++i) {
        if (sb[i]==-1) {
            dim_t quot = lb;
            for (int j=i+1; j<ra::size(sb); ++j) {
                quot *= sb[j];
                RA_CHECK(quot>0 && "cannot deduce dimensions");
            }
            auto pv = la/quot;
            RA_CHECK((la%quot==0 && pv>=0) && "bad placeholder");
            sb[i] = pv;
            lb = la;
            break;
        } else {
            lb *= sb[i];
        }
    }
    auto sa = shape(a);
// FIXME should be able to reshape Scalar etc.
    View<T, ra::size_s(sb)> b(map([](auto i) { return Dim { DIM_BAD, 0 }; }, ra::iota(ra::size(sb))), a.data());
    rank_t i = 0;
    for (; i<a.rank() && i<b.rank(); ++i) {
        if (sa[a.rank()-i-1]!=sb[b.rank()-i-1]) {
            assert(is_ravel_free(a) && "reshape w/copy not implemented");
            if (la>=lb) {
// FIXME View(SS const & s, T * p). Cf [ra37].
                for_each([](auto & dim, auto && s) { dim.size = s; }, b.dim, sb);
                filldim(b.dim.size(), b.dim.end());
                for (int j=0; j!=b.rank(); ++j) {
                    b.dim[j].stride *= a.stride(a.rank()-1);
                }
                return b;
            } else {
                assert(0 && "reshape case not implemented");
            }
        } else {
// select
            b.dim[b.rank()-i-1] = a.dim[a.rank()-i-1];
        }
    }
    if (i==a.rank()) {
// tile & return
        for (rank_t j=i; j<b.rank(); ++j) {
            b.dim[b.rank()-j-1] = { sb[b.rank()-j-1], 0 };
        }
    }
    return b;
}

template <class T, rank_t RANK, class S> inline
auto reshape(View<T, RANK> const & a, S && sb_)
{
    return reshape_(a, std::forward<S>(sb_));
}

// We need dimtype b/c {1, ...} deduces to int and that fails to match ra::dim_t.
// We could use initializer_list to handle the general case, but that would produce a var rank result because its size cannot be deduced at compile time :-/. Unfortunately an initializer_list specialization would override this one, so we cannot provide it as a fallback.
template <class T, rank_t RANK, class dimtype, int N> inline
auto reshape(View<T, RANK> const & a, dimtype const (&sb_)[N])
{
    return reshape_(a, sb_);
}

// lo = lower bounds, hi = upper bounds.
// The stencil indices are in [0 lo+1+hi] = [-lo +hi].
template <class LO, class HI, class T, rank_t N> inline
View<T, rank_sum(N, N)>
stencil(View<T, N> const & a, LO && lo, HI && hi)
{
    View<T, rank_sum(N, N)> s;
    s.p = a.data();
    ra::resize(s.dim, 2*a.rank());
    RA_CHECK(every(lo>=0));
    RA_CHECK(every(hi>=0));
    for_each([](auto & dims, auto && dima, auto && lo, auto && hi)
             {
                 RA_CHECK(dima.size>=lo+hi && "stencil is too large for array");
                 dims = {dima.size-lo-hi, dima.stride};
             },
             ptr(s.dim.data()), a.dim, lo, hi);
    for_each([](auto & dims, auto && dima, auto && lo, auto && hi)
             { dims = {lo+hi+1, dima.stride}; },
             ptr(s.dim.data()+a.rank()), a.dim, lo, hi);
    return s;
}

// Make last sizes of View<> be compile-time constants.
template <class super_t, rank_t SUPERR, class T, rank_t RANK> inline
auto explode_(View<T, RANK> const & a)
{
// TODO Reduce to single check, either the first or the second.
    static_assert(RANK>=SUPERR || RANK==RANK_ANY, "rank of a is too low");
    RA_CHECK(a.rank()>=SUPERR && "rank of a is too low");
    View<super_t, rank_sum(RANK, -SUPERR)> b;
    ra::resize(b.dim, a.rank()-SUPERR);
    dim_t r = 1;
    for (int i=0; i<SUPERR; ++i) {
        r *= a.size(i+b.rank());
    }
    RA_CHECK(r*sizeof(T)==sizeof(super_t) && "size of SUPERR axes doesn't match super type");
    for (int i=0; i<b.rank(); ++i) {
        RA_CHECK(a.stride(i) % r==0 && "stride of SUPERR axes doesn't match super type");
        b.dim[i].stride = a.stride(i) / r;
        b.dim[i].size = a.size(i);
    }
    RA_CHECK((b.rank()==0 || a.stride(b.rank()-1)==r) && "super type is not compact in array");
    b.p = reinterpret_cast<super_t *>(a.data());
    return b;
}

template <class super_t, class T, rank_t RANK> inline
auto explode(View<T, RANK> const & a)
{
    return explode_<super_t, (std::is_same_v<super_t, std::complex<T>> ? 1 : ra_traits<super_t>::rank_s())>(a);
}

// TODO Consider these in ra_traits<>.
template <class T, std::enable_if_t<is_scalar<T>, int> =0> inline int gstride(int i) { return 1; }
template <class T, std::enable_if_t<!is_scalar<T>, int> =0> inline int gstride(int i) { return T::stride(i); }
template <class T, std::enable_if_t<is_scalar<T>, int> =0> inline int gsize(int i) { return 1; }
template <class T, std::enable_if_t<!is_scalar<T>, int> =0> inline int gsize(int i) { return T::size(i); }

// TODO This routine is not totally safe; the ranks below SUBR must be compact, which is not checked.
template <class sub_t, class super_t, rank_t RANK> inline
auto collapse(View<super_t, RANK> const & a)
{
    using super_v = typename ra_traits<super_t>::value_type;
    using sub_v = typename ra_traits<sub_t>::value_type;
    constexpr int subtype = sizeof(super_v)/sizeof(sub_t);
    constexpr int SUBR = ra_traits<super_t>::rank_s()-ra_traits<sub_t>::rank_s();

    View<sub_t, rank_sum(RANK, SUBR+int(subtype>1))> b;
    resize(b.dim, a.rank()+SUBR+int(subtype>1));

    constexpr dim_t r = sizeof(super_t)/sizeof(sub_t);
    static_assert(sizeof(super_t)==r*sizeof(sub_t), "cannot make axis of super_t from sub_t");
    for (int i=0; i<a.rank(); ++i) {
        b.dim[i].stride = a.stride(i) * r;
        b.dim[i].size = a.size(i);
    }
    constexpr int t = sizeof(super_v)/sizeof(sub_v);
    constexpr int s = sizeof(sub_t)/sizeof(sub_v);
    static_assert(t*sizeof(sub_v)>=1, "bad subtype");
    for (int i=0; i<SUBR; ++i) {
        RA_CHECK(((gstride<super_t>(i)/s)*s==gstride<super_t>(i)) && "bad strides"); // TODO is actually static
        b.dim[a.rank()+i].stride = gstride<super_t>(i) / s * t;
        b.dim[a.rank()+i].size = gsize<super_t>(i);
    }
    if (subtype>1) {
        b.dim[a.rank()+SUBR].stride = 1;
        b.dim[a.rank()+SUBR].size = t;
    }
    b.p = reinterpret_cast<sub_t *>(a.data());
    return b;
}

// For functions that require compact arrays (TODO they really shouldn't).
template <class A> inline
bool const crm(A const & a)
{
    return ra::size(a)==0 || is_c_order(a);
}

} // namespace ra
