import os
import signal
import time
from os import killpg, getpgid, setsid, makedirs, listdir, remove
from os.path import join, exists
from subprocess import Popen
from threading import Thread
from PIL import Image
import rospy
from geometry_msgs.msg import PoseStamped
import traceback
import shutil
import re
import argparse

lastUpdate = 0


def killProcess(p):
    """
    Kills the process of the launchNavigation
    """
    killpg(getpgid(p.pid), signal.SIGTERM)


def getMap(p, folder):
    """
    Saves the map every 60 seconds
    """
    minutes = 0
    maxmapsave = 100
    seconds_mapsave = 60

    if not exists(join(folder, "Maps/")):
        makedirs(join(folder, "Maps/"))

    while True:
        if p.poll() is not None:
            return

        getmapString = (
            "rosrun map_server map_saver -f " + folder + "Maps/" + str(minutes) + "Map"
        )
        process = Popen(getmapString, shell=True, preexec_fn=setsid)
        process.wait()
        imStr = folder + "Maps/" + str(minutes)
        try:
            print(f"processing {imStr}Map.*")
            Image.open(imStr + "Map.pgm").save(imStr + "Map.png")
            remove(imStr + "Map.pgm")
        except IOError:
            print("cannot convert")

        if int(minutes) > maxmapsave:
            saveMap(folder)
            print("KILLING PROCESS DUE TO TIMEOUT")
            killProcess(p)
            return

        minutes += seconds_mapsave / 60
        time.sleep(seconds_mapsave)


def callback(data: PoseStamped):
    global lastUpdate
    lastUpdate = rospy.get_rostime().secs
    print(f"Received goal @ {lastUpdate} seconds (ROStime)")


def goal_listener():
    print("Started goal_listener")
    rospy.init_node("listener", anonymous=False, disable_signals=True)
    rospy.Subscriber("move_base/current_goal", PoseStamped, callback)


def saveMap(folder):
    if not exists(join(folder, "Maps/")):
        makedirs(join(folder, "Maps/"))
    getmapString = "rosrun map_server map_saver -f " + folder + "Maps/Map"
    process = Popen(getmapString, shell=True, preexec_fn=setsid)
    process.wait()
    imStr = folder + "Maps/"
    try:
        print(f"processing {imStr}Map.*")
        Image.open(imStr + "Map.pgm").save(imStr + "Map.png")
        remove(imStr + "Map.pgm")
    except IOError:
        print("cannot convert")


def checkActiveGoal(process, folder, waypoint_navigation):
    global lastUpdate
    MAX_EXPLORE = 5
    explore_num = 0
    print("Started goal Thread, ")
    while True:
        time.sleep(3)
        print(f"-- Delta since last update: {rospy.get_rostime().secs - lastUpdate} ")
        if process.poll() is not None or rospy.get_rostime().secs - lastUpdate > 500:
            if explore_num < MAX_EXPLORE:
                print("Restarting explore node to finish exploration")
                if waypoint_navigation:
                    p = Popen("rosnode kill waypoint_sender", shell=True)
                    p.wait()
                else:
                    p = Popen("rosnode kill explore", shell=True)
                    p.wait()
                explore_num += 1
                lastUpdate = rospy.get_rostime().secs - 100
                continue
            else:
                saveMap(folder)
                print("SHUTTING DOWN DUE TO GOAL TIMEOUT")
                killProcess(process)
                return


def waypointNavigation(p, folder):
    # TODO: save map each waypoint?
    print("Started waypoint navigation Thread.")
    launchString = f"rosrun exp_cov waypoint_navigation.py -p {os.path.expanduser('~/catkin_ws/src/my_navigation_configs/worlds/poses.txt')}"
    process = Popen(launchString, shell=True, preexec_fn=setsid)
    process.wait()
    saveMap(folder)
    killProcess(p)

def launchNavigation(world, folder, rectangles_path, no_bag, record_all_topics, waypoint_navigation):
    """
    Calls the launch file and starts the exploration, waits until the process is done
    """
    p = None
    bag_arg = "true"
    record_all_topics_arg = "false"
    if no_bag:
        bag_arg = "false"
    if record_all_topics:
        record_all_topics_arg = "true"
    try:
        if waypoint_navigation:
            launchString = f"roslaunch my_navigation_configs cover_ambient_slam_toolbox.launch worldfile:={world} bag:={folder}bag.bag record_bag:={bag_arg} bag_all:={record_all_topics_arg}"
        
            p = Popen(launchString, shell=True, preexec_fn=setsid)

            time.sleep(5)

            goal_listener()
            Thread(None, checkActiveGoal, "goal_worker", [p, folder, waypoint_navigation], daemon=True).start()
            Thread(None, getMap, "map_worker", [p, folder], daemon=True).start()
            Thread(group=None, target=waypointNavigation, name="waypoint_worker", args=[p, folder], daemon=True).start()
            p.wait()
            return

        else:
            launchString = (
                "roslaunch my_navigation_configs exploreambient_slam_toolbox.launch worldfile:="
                + world
                + " bag:="
                + folder
                + "bag"
                + ".bag "
                + " rectangles_path:=" 
                + rectangles_path
                + " record_bag:="
                + bag_arg
                + " bag_all:="
                + record_all_topics_arg
            )

            p = Popen(launchString, shell=True, preexec_fn=setsid)

            time.sleep(5)

            goal_listener()
            Thread(None, checkActiveGoal, "goal_worker", [p, folder, waypoint_navigation], daemon=True).start()
            Thread(None, getMap, "map_worker", [p, folder], daemon=True).start()
            p.wait()
            return

    except KeyboardInterrupt:
        print("KILL PROCESS")
        killProcess(p)
        return
    except:
        traceback.print_exc()
        killProcess(p)
        return

def extract_number(worldname):
    pattern = r"[a-zA-Z]+[\d]+.world"
    present = re.match(pattern, worldname)
    num = re.search(r"[\d]+", worldname)
    if present and num:
        print(num.group(0))
        return num.group(0)
    return None

def exploreWorlds(project_path, world_path, no_bag, record_all_topics, waypoint_navigation):
    """
    Given a folder with world file it runs exploration or coverage
    """
    out_dir = project_path + "/runs/outputs/"
    worldname = "UNKOWN"
    with open(world_path, "r", encoding="utf-8") as worldfile:
        worldfile.readline() # consume first empty line
        worldname = " ".join(worldfile.readline().split()[2:])
    folder = join(out_dir, worldname)
    print(f"PATH: {folder}")

    maxrun = 0

    if os.path.splitext(world_path)[1] == ".world":
        if not exists(folder):
            print(f"Making dir: {folder}")
            makedirs(folder)
        for i in listdir(folder):
            if int(i[3:]) >= maxrun:
                maxrun = int(i[3:])
        run_folder = join(folder, "run" + str(maxrun + 1) + "/")
        run_folder_bitmaps = os.path.join(run_folder, "bitmaps")
        if not exists(run_folder_bitmaps):
            makedirs(run_folder_bitmaps)

        #Save Bitmap, World and Rectangles too
        worldnum = extract_number(os.path.basename(world_path))
        if worldnum:
            rect_path = os.path.join(os.path.dirname(world_path), f"bitmaps/rectangles{worldnum}.json")
            bitmap_path = os.path.join(os.path.dirname(world_path), f"bitmaps/image{worldnum}.png")
            shutil.copy(rect_path, run_folder_bitmaps)
            shutil.copy(bitmap_path, run_folder_bitmaps)
            shutil.copy(world_path, run_folder)

            time.sleep(4)

            print("START")
            launchNavigation(world_path, run_folder, rect_path, no_bag, record_all_topics, waypoint_navigation)
            print("END")
            time.sleep(1)

def parse_args():
    parser = argparse.ArgumentParser(description='Start a single exploration run.')
    parser.add_argument("--world", metavar="WORLD_PATH", required=True,
        help="Path to world file.")    
    parser.add_argument("--no-bag",  action='store_true', default=False,
        help="Use this to disable bag recording, default behaviour is enabled.") 
    parser.add_argument("--bag-all",  action='store_true', default=False,
        help="Use this to record all topics, default behaviour is disabled, i.e. only /odom /base_pose_ground_truth and /base_scan are recorded.") 
    parser.add_argument("--waypoints",  action='store_true', default=False,
        help="Use this to set the working mode to coverage instead of greedy frontier-based exploration. If true it will look for a pose file at os.path.expanduser(\"~/catkin_ws/src/my_navigation_configs\") .")
    return parser.parse_args()

if __name__ == "__main__":
    time.sleep(4)
    args = parse_args()
    world_path = args.world
    no_bag = args.no_bag
    record_all_topics = args.bag_all
    waypoint_navigation = args.waypoints
    # retrieve current path
    project = os.path.expanduser("~/catkin_ws/src/my_navigation_configs")        
    exploreWorlds(project, world_path, no_bag, record_all_topics, waypoint_navigation)
