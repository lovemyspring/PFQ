#include <cstdio>
#include <string>
#include <stdexcept>

#include <pfq.hpp>
using namespace net;

int
main(int argc, char *argv[])
{
    if (argc < 2)
        throw std::runtime_error(std::string("usage: ").append(argv[0]).append(" dev"));
    
    pfq r(1514);

    r.add_device(argv[1], pfq::any_queue);

    r.toggle_time_stamp(true);
    
    r.enable();
    
    for(;;)
    {
            auto many = r.read( 1000000 /* timeout: micro */);
            
            std::cout << "batch size: " << many.size() << " ===>" << std::endl;
 
#if __GNUC__ == 4 &&  __GNUC_MINOR__ >= 6 

            for(auto & packet : many)
            {
                while(!packet.commit);

                // printf("caplen:%d len:%d ifindex:%d hw_queue:%d tstamp: %u:%u -> ", it->caplen, it->len, it->if_index, it->hw_queue,
                //                                                                    it->tstamp.tv.sec, it->tstamp.tv.nsec);
                char *buff = static_cast<char *>(data(packet));

                for(int x=0; x < std::min<int>(packet.caplen, 34); x++)
                {
                    printf("%2x ", (unsigned char)buff[x]);
                }
                printf("\n");
            }
#endif

    }

    return 0;
}
 
