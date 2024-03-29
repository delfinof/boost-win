/*
 *          Copyright Andrey Semashev 2007 - 2013.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   threadsafe_queue.cpp
 * \author Andrey Semashev
 * \date   05.11.2010
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 *
 * The implementation is based on algorithms published in the "Simple, Fast,
 * and Practical Non-Blocking and Blocking Concurrent Queue Algorithms" article
 * in PODC96 by Maged M. Michael and Michael L. Scott. Pseudocode is available here:
 * http://www.cs.rochester.edu/research/synchronization/pseudocode/queues.html
 *
 * The lock-free version of the mentioned algorithms contain a race condition and therefore
 * were not included here.
 */

#include <boost/log/detail/threadsafe_queue.hpp>

#ifndef BOOST_LOG_NO_THREADS

#include <stdlib.h>
#include <new>
#include <boost/assert.hpp>
#include <boost/static_assert.hpp>
#include <boost/throw_exception.hpp>
#include <boost/type_traits/alignment_of.hpp>
#include <boost/log/detail/spin_mutex.hpp>
#include <boost/log/detail/locks.hpp>
#include <boost/log/detail/alignas.hpp>

#if defined(BOOST_HAS_UNISTD_H)
#include <unistd.h> // _POSIX_VERSION
#endif

#if defined(BOOST_HAS_STDINT_H)
#include <stdint.h> // uintptr_t
#else
// MSVC defines integer types here
#include <stddef.h> // uintptr_t
#endif

#if defined(__APPLE__) || defined(__APPLE_CC__) || defined(macintosh)
#include <AvailabilityMacros.h>
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && (MAC_OS_X_VERSION_MIN_REQUIRED >= 1060)
// Mac OS X 10.6 and later have posix_memalign
#define BOOST_LOG_HAS_POSIX_MEMALIGN 1
#endif
#elif (defined(_POSIX_VERSION) && (_POSIX_VERSION >= 200112L)) || (defined(_XOPEN_SOURCE) && (_XOPEN_SOURCE >= 600))
// Solaris 10 does not have posix_memalign. Solaris 11 and later seem to have it.
#if !(defined(sun) || defined(__sun)) || defined(__SunOS_5_11) || defined(__SunOS_5_12)
#define BOOST_LOG_HAS_POSIX_MEMALIGN 1
#endif
#endif

#if defined(BOOST_WINDOWS)
#include <malloc.h> // _aligned_malloc, _aligned_free
#endif

#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

//! Generic queue implementation with two locks
class threadsafe_queue_impl_generic :
    public threadsafe_queue_impl
{
private:
    //! Mutex type to be used
    typedef spin_mutex mutex_type;

    /*!
     * A structure that contains a pointer to the node and the associated mutex.
     * The alignment below allows to eliminate false sharing, it should be not less than CPU cache line size (which is assumed to be 64 bytes in most cases).
     */
    struct BOOST_LOG_ALIGNAS(64) pointer
    {
        //! Pointer to the either end of the queue
        node_base* node;
        //! Lock for access synchronization
        mutex_type mutex;
        //  128 bytes padding is chosen to mitigate false sharing for NetBurst CPUs, which load two cache lines in one go.
        unsigned char padding[128U - (sizeof(node_base*) + sizeof(mutex_type)) % 128U];
    };

private:
    //! Pointer to the beginning of the queue
    pointer m_Head;
    //! Pointer to the end of the queue
    pointer m_Tail;

public:
    explicit threadsafe_queue_impl_generic(node_base* first_node)
    {
        set_next(first_node, NULL);
        m_Head.node = m_Tail.node = first_node;
    }

    ~threadsafe_queue_impl_generic()
    {
    }

    node_base* reset_last_node()
    {
        BOOST_ASSERT(m_Head.node == m_Tail.node);
        node_base* p = m_Head.node;
        m_Head.node = m_Tail.node = NULL;
        return p;
    }

    bool unsafe_empty()
    {
        return m_Head.node == m_Tail.node;
    }

    void push(node_base* p)
    {
        set_next(p, NULL);
        exclusive_lock_guard< mutex_type > _(m_Tail.mutex);
        set_next(m_Tail.node, p);
        m_Tail.node = p;
    }

    bool try_pop(node_base*& node_to_free, node_base*& node_with_value)
    {
        exclusive_lock_guard< mutex_type > _(m_Head.mutex);
        node_base* next = get_next(m_Head.node);
        if (next)
        {
            // We have a node to pop
            node_to_free = m_Head.node;
            node_with_value = m_Head.node = next;
            return true;
        }
        else
            return false;
    }

private:
    // Copying and assignment are closed
    threadsafe_queue_impl_generic(threadsafe_queue_impl_generic const&);
    threadsafe_queue_impl_generic& operator= (threadsafe_queue_impl_generic const&);

    BOOST_LOG_FORCEINLINE static void set_next(node_base* p, node_base* next)
    {
        p->next.data[0] = next;
    }
    BOOST_LOG_FORCEINLINE static node_base* get_next(node_base* p)
    {
        return static_cast< node_base* >(p->next.data[0]);
    }
};

BOOST_LOG_API threadsafe_queue_impl* threadsafe_queue_impl::create(node_base* first_node)
{
    return new threadsafe_queue_impl_generic(first_node);
}

BOOST_LOG_API void* threadsafe_queue_impl::operator new (std::size_t size)
{
    void* p = NULL;

#if defined(BOOST_LOG_HAS_POSIX_MEMALIGN)
    if (posix_memalign(&p, 64, size) || !p)
        BOOST_THROW_EXCEPTION(std::bad_alloc());
    return p;
#elif defined(BOOST_WINDOWS)
    p = _aligned_malloc(size, 64);
    if (!p)
        BOOST_THROW_EXCEPTION(std::bad_alloc());
#else
    p = malloc(size + 64);
    if (!p)
        BOOST_THROW_EXCEPTION(std::bad_alloc());
    unsigned char* q = static_cast< unsigned char* >(p) + 64;
    q = (unsigned char*)((uintptr_t)q & (~(uintptr_t)63));
    const unsigned char diff = q - static_cast< unsigned char* >(p);
    p = q;
    *--q = diff;
#endif

    return p;
}

BOOST_LOG_API void threadsafe_queue_impl::operator delete (void* p, std::size_t)
{
#if defined(BOOST_LOG_HAS_POSIX_MEMALIGN)
    free(p);
#elif defined(BOOST_WINDOWS)
    _aligned_free(p);
#else
    unsigned char* q = static_cast< unsigned char* >(p);
    const unsigned char diff = *--q;
    free(static_cast< unsigned char* >(p) - diff);
#endif
}

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_NO_THREADS
