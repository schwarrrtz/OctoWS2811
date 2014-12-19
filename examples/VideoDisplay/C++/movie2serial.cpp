#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

// dimensions of the video & the LEDs
static int width = 300;
static int height = 16;

static float max_framerate = 30.0;

std::string port_name_0 = "/dev/ttyACM0";
std::string port_name_1 = "/dev/ttyACM1";

namespace b_io = boost::asio;
namespace b_sys = boost::system;
namespace b_time = boost::posix_time;

int main(int argc, char* argv[])
{
    // initialize serial port
    b_io::io_service ioService;
    b_io::serial_port port0(ioService);
    b_io::serial_port port1(ioService);
    try
    {
        port0.open(port_name_0);
        port0.set_option(b_io::serial_port_base::baud_rate(115200));

        port1.open(port_name_1);
        port1.set_option(b_io::serial_port_base::baud_rate(115200));
    }
    catch(b_sys::system_error& e)
    {
        std::cerr << "error opening serial port - " << e.what() << std::endl;
        return 1;
    }
    
    // ask videoDisplay for format information
    char query = '?';
    b_io::write(port0, b_io::buffer(&query, sizeof(query)));
    b_io::streambuf queryBuf;
    b_io::read_until(port0, queryBuf, '\n');
    std::istream inStream(&queryBuf);
    std::string formatData;
    std::getline(inStream, formatData);
    std::cout << "format data: " << formatData << std::endl;
    
    // initialize ffmpeg
    FILE* ffmpegPipe = popen("ffmpeg -loglevel warning -f concat -i loop.txt -f image2pipe -pix_fmt rgb24 -vcodec rawvideo -", "r");
    if(!ffmpegPipe)
    {
        std::cerr << "failed to open ffmpeg process" << std::endl;
        return 1;
    }
    
    // write video data
    b_time::ptime oldTime;
    b_time::ptime newTime = b_time::microsec_clock::local_time();
    int frameCount = 0;    
    char* inBuf = (char*)malloc(3*width*height*sizeof(char));
    int* pixBuf0 = (int*)malloc(width*8*sizeof(int));
    int* pixBuf1 = (int*)malloc(width*8*sizeof(int));
    char* outBuf0 = (char*)malloc(3*(width*8 + 1)*sizeof(char));
    char* outBuf1 = (char*)malloc(3*(width*8 + 1)*sizeof(char));
    outBuf0[0] = '*'; outBuf0[1] = '0'; outBuf0[2] = '0';
    outBuf1[0] = '*'; outBuf1[1] = '0'; outBuf1[2] = '0';

    while(!feof(ffmpegPipe))
    { 
        // fill data frame from ffmpeg pipe
        size_t bytesRead = 0;
        while((bytesRead < 3*width*height) && !feof(ffmpegPipe))
        {
            bytesRead += fread((inBuf + bytesRead + 3), sizeof(char), 3*width*height - bytesRead, ffmpegPipe);
        }
        
        if (bytesRead != 3*width*height)
        {
            std::cerr << "error reading frame" << std::endl;
            return 1;
        }
    
        // swizzle colours (input is rgb, chipset requires grb)
        // the first 3 bytes are format data that we don't touch
        for(int pixIndex = 0; pixIndex < width*height; pixIndex++)
        {  
            int green = inBuf[3*(pixIndex+1) + 1] << 16;
            int red = inBuf[3*(pixIndex+1)] << 8;
            int blue = inBuf[3*(pixIndex+1) + 2];

            if(pixIndex > width*8)
            {
                pixBuf1[pixIndex - width*8] = (int)(green | red | blue);
            }
            else
            {
                pixBuf0[pixIndex] = (int)(green | red | blue);
            }
        }

        // serialize in the format that the teensy is expecting
        int offset = 3;
        for(int xIndex = 0; xIndex < width; xIndex++)
        {
            for(int mask = 0x800000; mask > 0; mask = mask >> 1)
            {
                char outByte0 = 0;
                char outByte1 = 0;
                for(int yIndex = 0; yIndex < 8; yIndex++)
                {
                    if((pixBuf0[xIndex + width*yIndex] & mask) != 0)
                    {
                        outByte0 |= (1 << yIndex);
                    }
                    if((pixBuf1[xIndex + width*yIndex] & mask) != 0)
                    {
                        outByte1 |= (1 << yIndex);
                    }
                }
                outBuf0[offset] = outByte0;
                outBuf1[offset++] = outByte1;
            }
        }

        // synchronous write
        b_io::write(port0, b_io::buffer(outBuf0, 3*(width*8 + 1)));
        b_io::write(port1, b_io::buffer(outBuf1, 3*(width*8 + 1)));

        // calculate framerate & throttle if necessary        

        usleep(50000);

        /*
        oldTime = newTime;
        newTime = b_time::microsec_clock::local_time();
        b_time::time_duration delta = newTime - oldTime;
        int delay = 1000000/max_framerate - 1000*delta.total_milliseconds();
        if(delay > 0)
        {
            usleep(delay);
        }    
        else if(((frameCount + 1) % 10) == 0)
        {
            std::cout << "framerate: " << ((delay > 0) ? 30 : delta.total_milliseconds() / 1000) << std::endl;
        }
        */
        
        frameCount++;
    }

    free(inBuf);
    free(pixBuf0);
    free(pixBuf1);
    free(outBuf0);
    free(outBuf1);

    std::cout << "read " << frameCount << " frames successfully" << std::endl;

    // finalize
    port0.close();   
    port1.close();             
    return pclose(ffmpegPipe);
}
