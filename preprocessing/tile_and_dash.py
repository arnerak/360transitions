# ffprobe, ffmpeg and mp4box required
#############################################################
vidfile = "dive.mkv"
htiles = 4
vtiles = 4
segmentDuration = 1500 # ms
bitrateLevels = [ 1, 0.25, 0.0625 ]
startFrom = 40
outLength = 30
#############################################################

import subprocess
import sys
import re
import os
import xml.etree.ElementTree as ET
from shutil import copyfile

def probeFile(file, properties):
    resolutionQuery = "ffprobe -v error -select_streams v:0 -show_entries stream=%s -of default=nw=1 %s" % (properties, file)
    proc = subprocess.Popen(resolutionQuery, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out,err = proc.communicate()
    out = out.decode("utf-8")
    return list(map(int, re.findall(r'\d+', out))),err

def runProc(query):
    FNULL = open(os.devnull, 'w')
    proc = subprocess.Popen(query, stdout=FNULL, stderr=subprocess.STDOUT)
    proc.wait()
    
wh,err = probeFile(vidfile, "width,height,r_frame_rate")

if len(wh) != 4:
	print(err)
	sys.exit()

width = wh[0]
height = wh[1]
fps = float(wh[2]) / float(wh[3])
keyint = int(math.ceil(fps) * (segmentDuration / 1000.0))
print("Video resolution: " + str(width) + "x" + str(height))
print("Video fps: " + str(fps))

twidth = int(width / htiles)
theight = int(height / vtiles)
print("Tile resolution: " + str(twidth) + "x" + str(theight))

if not os.path.isdir("tmp"):
    os.makedirs("tmp")

vidname = vidfile.split('.')[0]

mpds = []
for i in range(0, len(bitrateLevels)):
    qvidname = '%s%d' % (vidname, i)
    mpds.append( qvidname + ".mpd")

# print("Encoding quality levels...")
# for i in range(0, len(bitrateLevels)):
    # qvidname = '%s%d' % (vidname, i)
    # qtmpname = 'tmp\\%s.mp4' % (qvidname)
    # mpds.append( qvidname + ".mpd")
    # if os.path.isfile(qtmpname):
        # print("\r%d/%d encoded" % (i+1, len(bitrateLevels)), end='')
        # continue
    # if not bitrateLevels[i] == 1:
        # runProc("ffmpeg -i %s -b %d %s" % (vidfile, bitrateLevels[i] * bitrate, qtmpname))
    # else:
        # copyfile(vidfile, qtmpname)
    # print("\r%d/%d encoded" % (i+1, len(bitrateLevels)), end='')
# print("")

print("Cropping...")
counter = 0
for x in range(0, htiles):
    for y in range(0, vtiles):
        tilebr = -1
        for b in range(0, len(bitrateLevels)):
            #tmpvidfile = 'tmp\\%s%d.mp4' % (vidname, b)
            tmpvidfile = vidfile
            croppedfile = "tmp\\%s_%d_%d_%d.mp4" % (vidname, b, x, y)
            counter = counter + 1
            if os.path.isfile(croppedfile):
                print("\r%d/%d cropped" % (counter, htiles * vtiles * len(bitrateLevels)), end='')
                continue
            if tilebr == -1:
                runProc("ffmpeg -i %s -filter:v \"crop=%d:%d:%d:%d,fps=%d\" -codec:v libx264 -x264opts keyint=%d:min-keyint=%d:scenecut=-1 -an -ss %d -t %d -y %s" % \
                    (tmpvidfile, twidth, theight, x * twidth, y * theight, math.ceil(fps), keyint, keyint, startFrom, outLength, croppedfile))
                tilebr = probeFile(croppedfile, "bit_rate")[0][0]
                if not bitrateLevels[b] == 1:
                    runProc("ffmpeg -i %s -b %d -filter:v \"crop=%d:%d:%d:%d,fps=%d\" -codec:v libx264 -x264opts keyint=%d:min-keyint=%d:scenecut=-1 -an -ss %d -t %d -y %s" % \
                        (tmpvidfile, tilebr * bitrateLevels[b], twidth, theight, x * twidth, y * theight, math.ceil(fps), keyint, keyint, startFrom, outLength, croppedfile))
            else:
                runProc("ffmpeg -i %s -b %d -filter:v \"crop=%d:%d:%d:%d,fps=%d\" -codec:v libx264 -x264opts keyint=%d:min-keyint=%d:scenecut=-1 -an -ss %d -t %d -y %s" % \
                    (tmpvidfile, tilebr * bitrateLevels[b], twidth, theight, x * twidth, y * theight, math.ceil(fps), keyint, keyint, startFrom, outLength, croppedfile))

            print("\r%d/%d cropped" % (counter, htiles * vtiles * len(bitrateLevels)), end='')
print("")

foldername = vidname + "_dash"

if not os.path.isdir(foldername):
	os.makedirs(foldername)

for b in range(0, len(bitrateLevels)):
    dashQuery = "mp4box -dash-strict %d -frag %d -rap -frag-rap -out %s -segment-name %s\\%%s_" % (segmentDuration, segmentDuration, mpds[b], foldername)
    i = 0
    for x in range(0, htiles):
        for y in range(0, vtiles):
            file = "tmp\\%s_%d_%d_%d.mp4" % (vidname, b, x, y)
            dashQuery = dashQuery + " %s:desc_as=\"<SupplementalProperty schemeIdUri=\"\"urn:mpeg:dash:srd:2014\"\" value=\"\"%d,%d,%d,%d,%d,%d,%d\"\"/>\"" % (file, i, x * twidth, y * theight, twidth, theight, htiles, vtiles)
            i = i + 1
    proc = subprocess.Popen(dashQuery)
    proc.wait()

# merge mpds to single mpd
ns = "urn:mpeg:dash:schema:mpd:2011"
ET.register_namespace('', ns)
ns = "{%s}" % (ns)
mpd1 = ET.parse(mpds[0])
root = mpd1.getroot()
period = root.find(ns + 'Period')
adaptionSets = period.findall(ns + 'AdaptationSet')

for i in range(1, len(mpds)):
    otherAdaptionSets = ET.parse(mpds[i]).getroot().find(ns + 'Period').findall(ns + 'AdaptationSet')
    for j in range(0, len(adaptionSets)):
        adaptionSets[j].append(otherAdaptionSets[j].find(ns + 'Representation'))

for mpd in mpds:
    os.remove(mpd)
        
mpd1.write('%s.mpd' % (vidname), encoding='utf8')
    
print("done")
