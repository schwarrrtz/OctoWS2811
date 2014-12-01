# this program decodes video files using ffmpeg, serializes them, and sends them to the teensy
# ffmpeg built for RasPi is available at http://files.oz9aec.net/datv/490-rpi-hdtv/ffmpeg

import subprocess, numpy, serial, time, math

# dimensions of both the video and the LEDs
width = 300
height = 8

command = 	[ 'ffmpeg',
		'-loglevel', 'warning',	
		'-f', 'concat',
		#'-r', '10',
		'-i', 'loop.txt',
		#'-r', '1',
		#'-filter:v', "select='mod(n-1, 10)'",
		'-f', 'image2pipe',	
		'-pix_fmt', 'rgb24',
		'-vcodec', 'rawvideo', '-']

# configure serial port & data frame. set writeTimeout to 0 for non-blocking write
ffmpeg_process = subprocess.Popen(command, stdout = subprocess.PIPE, bufsize=10^8)
port = serial.Serial('/dev/ttyACM0', 115200, timeout=1, writeTimeout=0)
outData = bytearray(3*width*height + 3) 		# 3 bytes of format data:
outData[0] = ord('*') 					# command character
outData[1] = 0						# frame sync timing byte 0
outData[2] = 0						# frame sync timing byte 1

# query VideoDisplay for format info
port.write('?')
time.sleep(0.5)
format_data = port.readline()
format_string = format_data.decode(encoding='utf-8')
format_list = format_string.split(',')
if len(format_list) != 12:
	print "error reading format data from teensy. format length is " + str(len(format_list))
	port.close()
	ffmpeg_process.kill()
	exit()
print "LED_WIDTH: " + format_list[0]
print "LED_HEIGHT: " + format_list[1]
print "LED_LAYOUT: " + format_list[2]
print "VIDEO_XOFFSET: " + format_list[5]
print "VIDEO_YOFFSET: " + format_list[6]
print "VIDEO_WIDTH: " + format_list[7]
print "VIDEO_HEIGHT: " + format_list[8]
print

# start decoding & sending frames to teensy
previousTime = 0
currentTime = time.time()
frame_count = 0
while True:
	raw_image = ffmpeg_process.stdout.read(width*height*3)
	if len(raw_image) != width*height*3: 
		print "error reading frame / finished reading file"
		print "read " + str(frame_count) + " frames successfully"
		break
	
	image = list(raw_image)

	for index in range(0, len(image)):	
		outData[index+3] = image[index]

	port.write(outData)
	if (frame_count + 1) % 10 == 0:
		previousTime = currentTime
		currentTime = time.time()
		print "framerate is " + str(10/(currentTime - previousTime))
	frame_count = frame_count + 1

# clean up
port.close()
ffmpeg_process.kill()


