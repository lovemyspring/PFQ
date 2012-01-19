/***************************************************************
 *                                                
 * (C) 2011 - Nicola Bonelli <nicola.bonelli@cnit.it>   
 *
 ***************************************************************/

#ifndef _PFQ_HPP_
#define _PFQ_HPP_ 

#include <linux/if_ether.h>
#include <linux/pf_q.h>

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/mman.h>
#include <poll.h>

#include <iostream>
#include <stdexcept>
#include <iterator>
#include <cstring>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <thread>

namespace net { 

    typedef std::pair<char *, size_t> mutable_buffer;
    typedef std::pair<const char *, const size_t> const_buffer;

    template<size_t N, typename T>
    inline T align(T value)
    {
        static_assert((N & (N-1)) == 0, "align: N not a power of two");
        return (value + (N-1)) & ~(N-1);
    }

    static inline
    void mb()  { asm volatile ("": : :"memory"); }

    static inline
    void wmb() { asm volatile ("": : :"memory"); }
    
    static inline
    void rmb() { asm volatile ("": : :"memory"); }

    class batch 
    {
    public:
         
        struct const_iterator;

        /* simple forward iterator over frames */
        struct iterator : public std::iterator<std::forward_iterator_tag, pfq_hdr>
        {
            friend struct batch::const_iterator;

            iterator(pfq_hdr *h, size_t slot_size)
            : hdr_(h), slot_size_(slot_size)
            {}

            ~iterator() = default;
            
            iterator(const iterator &other)
            : hdr_(other.hdr_), slot_size_(other.slot_size_)
            {}

            iterator & 
            operator++()
            {
                hdr_ = reinterpret_cast<pfq_hdr *>(
                        reinterpret_cast<char *>(hdr_) + slot_size_);
                return *this;
            }
            
            iterator 
            operator++(int)
            {
                iterator ret(*this);
                ++(*this);
                return ret;
            }

            pfq_hdr *
            operator->() const
            {
                return hdr_;
            }

            pfq_hdr &
            operator*() const                                  
            {
                return *hdr_;
            }

            char *
            data() const
            {
                return reinterpret_cast<char *>(hdr_+1);
            }

            bool 
            operator==(const iterator &other) const
            {
                return hdr_ == other.hdr_;
            }

            bool
            operator!=(const iterator &other) const
            {
                return !(*this == other);
            }

        private:
            pfq_hdr *hdr_;
            size_t   slot_size_;
        };

        /* simple forward const_iterator over frames */
        struct const_iterator : public std::iterator<std::forward_iterator_tag, pfq_hdr>
        {
            const_iterator(pfq_hdr *h, size_t slot_size)
            : hdr_(h), slot_size_(slot_size)
            {}

            const_iterator(const const_iterator &other)
            : hdr_(other.hdr_), slot_size_(other.slot_size_)
            {}

            const_iterator(const batch::iterator &other)
            : hdr_(other.hdr_), slot_size_(other.slot_size_)
            {}

            ~const_iterator() = default;

            const_iterator & 
            operator++()
            {
                hdr_ = reinterpret_cast<pfq_hdr *>(
                        reinterpret_cast<char *>(hdr_) + slot_size_);
                return *this;
            }
            
            const_iterator 
            operator++(int)
            {
                const_iterator ret(*this);
                ++(*this);
                return ret;
            }

            const pfq_hdr *
            operator->() const
            {
                return hdr_;
            }

            const pfq_hdr &
            operator*() const
            {
                return *hdr_;
            }

            const char *
            data() const
            {
                return reinterpret_cast<const char *>(hdr_+1);
            }

            bool 
            operator==(const const_iterator &other) const
            {
                return hdr_ == other.hdr_;
            }

            bool
            operator!=(const const_iterator &other) const
            {
                return !(*this == other);
            }

        private:
            pfq_hdr *hdr_;
            size_t   slot_size_;
        };

    public:
        batch(void *addr, uint32_t slot_size, uint32_t batch_len)
        : addr_(addr), slot_size_(slot_size), batch_len_(batch_len)
        {}

        ~batch() = default;

        size_t
        size() const
        {
            // return the number of packets in this batch.
            return batch_len_;
        }

        size_t
        slot_size() const
        {
            return slot_size_;
        }

        const void *
        data() const
        {
            return addr_;
        }

        iterator
        begin()  
        {
            return iterator(reinterpret_cast<pfq_hdr *>(addr_), slot_size_);
        }

        const_iterator
        begin() const  
        {
            return const_iterator(reinterpret_cast<pfq_hdr *>(addr_), slot_size_);
        }

        iterator
        end()  
        {
            return iterator(reinterpret_cast<pfq_hdr *>(
                        static_cast<char *>(addr_) + batch_len_ * slot_size_), slot_size_);
        }

        const_iterator
        end() const 
        {
            return const_iterator(reinterpret_cast<pfq_hdr *>(
                        static_cast<char *>(addr_) + batch_len_ * slot_size_), slot_size_);
        }

        const_iterator
        cbegin() const
        {
            return const_iterator(reinterpret_cast<pfq_hdr *>(addr_), slot_size_);
        }

        const_iterator
        cend() const 
        {
            return const_iterator(reinterpret_cast<pfq_hdr *>(
                        static_cast<char *>(addr_) + batch_len_ * slot_size_), slot_size_);
        }

    private:
        void    *addr_;
        uint32_t slot_size_;
        uint32_t batch_len_;
    };

    //////////////////////////////////////////////////////////////////////

    class pfq
    {
    public:

        static const int any_device = Q_ANY_DEVICE;
        static const int any_queue  = Q_ANY_QUEUE;

        pfq()
        : fd_(-1)
        , queue_addr_(NULL)
        , queue_size_(0)
        , queue_slots_(0)
        , queue_caplen_(0)
        , slot_size_(0)
        , next_len_(0)
        {
            fd_ = ::socket(PF_Q, SOCK_RAW, htons(ETH_P_ALL));
            if (fd_ == -1)
                throw std::runtime_error("PFQ module not loaded");
            
            socklen_t size = sizeof(queue_slots_);
            if (::getsockopt(fd_, PF_Q, SO_GET_SLOTS, &queue_slots_, &size) == -1)
                throw std::runtime_error("PFQ: SO_GET_SLOTS");
            
            size = sizeof(queue_caplen_);
            if (::getsockopt(fd_, PF_Q, SO_GET_CAPLEN, &queue_caplen_, &size) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);

            slot_size_ = align<8>(sizeof(pfq_hdr) + queue_caplen_);
        }

        pfq(size_t caplen, size_t slots = 131072)
        : fd_(-1)
        , queue_addr_(NULL)
        , queue_size_(0)
        , queue_slots_(0)
        , queue_caplen_(0)
        , slot_size_(0)
        , next_len_(0)
        {
            fd_ = ::socket(PF_Q, SOCK_RAW, htons(ETH_P_ALL));
            if (fd_ == -1)
                throw std::runtime_error("PFQ module not loaded");
            
            if (::setsockopt(fd_, PF_Q, SO_SLOTS, &slots, sizeof(slots)) == -1)
                throw std::runtime_error("PFQ: SO_SLOTS");
            queue_slots_ = slots;
            
            if (::setsockopt(fd_, PF_Q, SO_CAPLEN, &caplen, sizeof(caplen)) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
            queue_caplen_ = caplen;
            
            slot_size_ = align<8>(sizeof(pfq_hdr) + queue_caplen_);
        }

        ~pfq()
        {
            this->close();
        }

        pfq(const pfq&) = delete;
        pfq& operator=(const pfq&) = delete;

        pfq(pfq &&other)
        : fd_(other.fd_)
        , queue_addr_(other.queue_addr_)
        , queue_size_(other.queue_size_)
        , queue_slots_(other.queue_slots_)
        , queue_caplen_(other.queue_caplen_)
        , slot_size_(0)
        , next_len_(0)
        {
            slot_size_ = align<8>(sizeof(pfq_hdr) + queue_caplen_);
            other.fd_ = -1;
        }

        pfq& 
        operator=(pfq &&other)
        {
            if (this != &other)
            {
                this->close();

                fd_ = other.fd_;
                queue_addr_   = other.queue_addr_;
                queue_size_   = other.queue_size_;
                queue_slots_  = other.queue_slots_;
                queue_caplen_ = other.queue_caplen_;
                slot_size_    = other.slot_size_;
                next_len_     = other.next_len_;
            }
            return *this;
        }
        
        void swap(pfq &other)
        {
            std::swap(fd_,          other.fd_);
            std::swap(queue_addr_,  other.queue_addr_);
            std::swap(queue_size_,  other.queue_size_);
            std::swap(queue_slots_, other.queue_slots_);
            std::swap(queue_caplen_,other.queue_caplen_);
            std::swap(slot_size_,   other.slot_size_);
            std::swap(next_len_,    other.next_len_);
        }                           
                
        void enable()
        {
            int one = 1;
            if(::setsockopt(fd_, PF_Q, SO_TOGGLE_QUEUE, &one, sizeof(one)) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
            
            size_t tot_mem; socklen_t size = sizeof(tot_mem);
            
            if (::getsockopt(fd_, PF_Q, SO_GET_QUEUE_MEM, &tot_mem, &size) == -1)
                throw std::runtime_error("PFQ: SO_GET_QUEUE_MEM");
            
            queue_size_ = tot_mem;

            if ((queue_addr_ = mmap(NULL, tot_mem, PROT_READ|PROT_WRITE, MAP_SHARED, fd_, 0)) == MAP_FAILED) 
                throw std::runtime_error("PFQ: mmap error");
        }

        void disable()
        {
            if (munmap(queue_addr_, queue_size_) == -1)
                throw std::runtime_error(std::string(__PRETTY_FUNCTION__).append(": munmap"));
            
            queue_addr_ = NULL;
            queue_size_ = 0;

            int one = 0;
            if(::setsockopt(fd_, PF_Q, SO_TOGGLE_QUEUE, &one, sizeof(one)) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
        }

        bool is_enabled() const
        {
            if (fd_ != -1)
            {
                int ret; socklen_t size = sizeof(ret);

                if (::getsockopt(fd_, PF_Q, SO_GET_STATUS, &ret, &size) == -1)
                    throw std::runtime_error(__PRETTY_FUNCTION__);
                return ret;
            }
            return false;
        }

        void load_balance(bool value)
        {
            int one = value;
            if (::setsockopt(fd_, PF_Q, SO_LOAD_BALANCE, &one, sizeof(one)) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
        }

        int ifindex(const char *dev) const
        {
            struct ifreq ifreq_io;
            strncpy(ifreq_io.ifr_name, dev, IFNAMSIZ);
            if (::ioctl(fd_, SIOCGIFINDEX, &ifreq_io) == -1)
                return -1;
            return ifreq_io.ifr_ifindex;
        }

        void tstamp(bool value)
        {
            size_t ts = static_cast<int>(value);
            if (::setsockopt(fd_, PF_Q, SO_TSTAMP_TYPE, &ts, sizeof(int)) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
        }

        bool tstamp() const
        {
           int ret; socklen_t size = sizeof(int);
           if (::getsockopt(fd_, PF_Q, SO_GET_TSTAMP_TYPE, &ret, &size) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
           return ret;
        }

        void caplen(size_t value)
        {
            if (is_enabled()) 
                throw std::runtime_error(__PRETTY_FUNCTION__);
            
            if (::setsockopt(fd_, PF_Q, SO_CAPLEN, &value, sizeof(value)) == -1) {
                throw std::runtime_error(__PRETTY_FUNCTION__);
            }

            slot_size_ = align<8>(sizeof(pfq_hdr)+ value);
        }

        size_t caplen() const
        {
           size_t ret; socklen_t size = sizeof(ret);
           if (::getsockopt(fd_, PF_Q, SO_GET_CAPLEN, &ret, &size) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
           return ret;
        }

        void
        slots(size_t value) 
        {             
            if (is_enabled()) 
                throw std::runtime_error(__PRETTY_FUNCTION__);
                      
            if (::setsockopt(fd_, PF_Q, SO_SLOTS, &value, sizeof(value)) == -1) {
                throw std::runtime_error(__PRETTY_FUNCTION__);
            }

            queue_slots_ = value;
        }
        
        size_t
        slots() const
        {
            return queue_slots_;
        }

        size_t slot_size() const
        {
            return slot_size_;
        }

        void add_device(int index, int queue = any_queue)
        {
            struct pfq_dev_queue dq = { index, queue };
            if (::setsockopt(fd_, PF_Q, SO_ADD_DEVICE, &dq, sizeof(dq)) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
        }
        
        void add_device(const char *dev, int queue = any_queue)
        {
            auto index = ifindex(dev);
            if (index == -1)
                throw std::runtime_error(std::string(__PRETTY_FUNCTION__).append(": device not found"));
            add_device(index, queue);
        }                              

        void remove_device(int index, int queue = any_queue)
        {
            struct pfq_dev_queue dq = { index, queue };
            if (::setsockopt(fd_, PF_Q, SO_REMOVE_DEVICE, &dq, sizeof(dq)) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
        }
        void remove_device(const char *dev, int queue = any_queue)
        {
            auto index = ifindex(dev);
            if (index == -1)
                throw std::runtime_error(std::string(__PRETTY_FUNCTION__).append(": device not found"));
            remove_device(index, queue);
        }  

        unsigned long 
        owners(int index, int queue) const
        {
            struct pfq_dev_queue dq = { index, queue };
            socklen_t s = sizeof(struct pfq_dev_queue);

            if (::getsockopt(fd_, PF_Q, SO_GET_OWNERS, &dq, &s) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
            return *reinterpret_cast<unsigned long *>(&dq);
        }
        unsigned long 
        owners(const char *dev, int queue) const
        {
            auto index = ifindex(dev);
            if (index == -1)
                throw std::runtime_error(std::string(__PRETTY_FUNCTION__).append(": device not found"));
            return owners(index,queue);
        }

        int 
        poll(long int microseconds = -1 /* infinite */)
        {
            struct pollfd fd = {fd_, POLLIN, 0 };
            struct timespec timeout = { microseconds/1000000, (microseconds%1000000) * 1000};

            int ret = ::ppoll(&fd, 1, microseconds < 0 ? NULL : &timeout, NULL);
            if (ret < 0)
               throw std::runtime_error(std::string(__PRETTY_FUNCTION__).append(strerror(errno)));
            return ret; 
        }

        batch
        read(long int microseconds = -1) 
        {
            struct pfq_queue_descr * q = static_cast<struct pfq_queue_descr *>(queue_addr_);

            int data =  q->data;               
            int index  = DBMP_QUEUE_INDEX(data) ? 1 : 0;
            
            size_t q_size = queue_slots_ * slot_size_;

            //  watermark for polling...
            //
            if( DBMP_QUEUE_LEN(data) < (queue_slots_ >> 1) ) {
                this->poll(microseconds);
            }

            // clean the next buffer...
            //
            { 
                char * p = static_cast<char *>(queue_addr_) + sizeof(pfq_queue_descr) + !index * q_size;
                for(unsigned int i = 0; i < next_len_; i++)
                {
                    *reinterpret_cast<uint64_t *>(p) = 0; // h->commit = 0; (just a bit faster)
                    p += slot_size_;
                }
            }

            // compiler barrier
            //
            wmb();

            // atomic exchange: swap the queues...
            // 
            data = __sync_lock_test_and_set(&q->data, (index ? 0ULL : 0x8000000000000000ULL));
            
            // just in case the queue was blocked, re-enable it
            //
            q->disabled = 0;

            // std::cout << "REAL_LEN: " << DBMP_QUEUE_LEN(data) << std::endl;

            next_len_ =  std::min(static_cast<size_t>(DBMP_QUEUE_LEN(data)), queue_slots_);

            return batch(static_cast<char *>(queue_addr_) + sizeof(pfq_queue_descr) + index * q_size, slot_size_, next_len_);
        }
        
        batch
        recv(const mutable_buffer &buff, long int microseconds = -1) 
        {
            auto this_batch = this->read(microseconds);
            
            if (buff.second < queue_slots_ * slot_size_)
                throw std::runtime_error(std::string(__PRETTY_FUNCTION__).append(": buffer too short"));

            memcpy(buff.first, this_batch.data(), this_batch.slot_size() * this_batch.size());
            return batch(buff.first, this_batch.slot_size(), this_batch.size());
        }

        pfq_stats
        stats() const
        {
            pfq_stats stat;
            socklen_t size = sizeof(struct pfq_stats);
            if (::getsockopt(fd_, PF_Q, SO_GET_STATS, &stat, &size) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
            return stat;
        }

        size_t
        mem_size() const
        {
            return queue_size_;
        }

        const void *
        mem_addr() const
        {
            return queue_addr_;
        }

        int id() const
        {
            if (fd_ == -1)
                return -1;

            int ret; socklen_t size = sizeof(int);
            if (::getsockopt(fd_, PF_Q, SO_GET_ID, &ret, &size) == -1)
                throw std::runtime_error(__PRETTY_FUNCTION__);
            return ret;
        }

    private:
        void close()
        {
            if (fd_ != -1) {
                ::close(fd_);
                fd_ = -1;
            }
        }

        int fd_;

        void * queue_addr_;
        size_t queue_size_;
        size_t queue_slots_; 
        size_t queue_caplen_;
        size_t slot_size_;
        size_t next_len_;
    };


    template <typename CharT, typename Traits>
    typename std::basic_ostream<CharT, Traits> &
    operator<<(std::basic_ostream<CharT,Traits> &out, const pfq_stats& rhs)
    {
        return out << rhs.recv << ' ' << rhs.lost << ' ' << rhs.drop;
    }

    inline pfq_stats&
    operator+=(pfq_stats &lhs, const pfq_stats &rhs)
    {
        lhs.recv += rhs.recv;
        lhs.lost += rhs.lost;
        lhs.drop += rhs.drop;
        return lhs;
    }
    
    inline pfq_stats&
    operator-=(pfq_stats &lhs, const pfq_stats &rhs)
    {
        lhs.recv -= rhs.recv;
        lhs.lost -= rhs.lost;
        lhs.drop -= rhs.drop;
        return lhs;
    }

    inline pfq_stats
    operator+(pfq_stats lhs, const pfq_stats &rhs)
    {
        lhs += rhs;
        return lhs;
    }

    inline pfq_stats
    operator-(pfq_stats lhs, const pfq_stats &rhs)
    {
        lhs -= rhs;
        return lhs;
    }

} // namespace net

#endif /* _PFQ_HPP_ */
