import subprocess as sp
import os
from concurrent.futures import ThreadPoolExecutor
import progressbar
import time
import argparse
import shutil


def make_map(args, world_num):
    image_path = args.map
    movement_mask_image_path = args.mask
    speedup = args.speedup
    pose = args.pose
    scale = args.scale

    sp.run(["python3", os.path.join(os.getcwd(), "src/exp_cov/scripts/map_rgb_simul.py"), "--map", image_path, "--mask", movement_mask_image_path,
        "--dir", os.path.join(os.getcwd(), "worlds"), "--speedup", str(speedup), "--pose", f"{pose[0]} {pose[1]}", "--scale", str(scale), "--world-num", str(world_num), "--silent"])

    return(f"world{world_num}.world")

def spawn_container(i, bar, no_bag, args):
    time.sleep(4)
    mapName = make_map(args, i)

    run_dir = os.path.join(args.dir, "RUN")
    bag_option = ""
    record_all_option = ""
    waypoint_option = ""
    if no_bag:
        bag_option = "--no-bag"
    if args.bag_all:
        record_all_option = "--bag-all"
        run_dir += "_DEBUG"
    os.makedirs(run_dir, exist_ok = True)
    if args.coverage:
        waypoint_option = "--waypoints"
    launchstr = f"""docker run -it \\
        --mount type=bind,source=./worlds,target=/root/catkin_ws/src/my_navigation_configs/worlds \\
        --mount type=bind,source=./src/my_navigation_configs/params,target=/root/catkin_ws/src/my_navigation_configs/params \\
        --mount type=bind,source=./src/exp_cov/param,target=/root/catkin_ws/src/exp_cov/param \\
        --mount type=bind,source={run_dir},target=/root/catkin_ws/src/my_navigation_configs/runs/outputs \\
        'rosnoetic:slam_toolbox' /root/catkin_ws/src/my_navigation_configs/worlds/{mapName} {bag_option} {record_all_option} {waypoint_option}"""
    p = sp.Popen(launchstr, shell=True, stdout=sp.DEVNULL)
    p.wait()
    bar.increment()

def purge_worlds():
    not_to_delete = ("rgb.world", "image.png", "rectangles.json")
    for root, _, files in os.walk("worlds/"):
        for file in files:
            if file not in not_to_delete:
                os.remove(os.path.join(root, file))

def main(workers: int, no_bag, args):
    pool = ThreadPoolExecutor(max_workers=workers)
    try:
        if args.coverage:
            shutil.copyfile(args.poses, os.path.join(os.getcwd(), "worlds", "poses.txt"))
        with progressbar.ProgressBar(max_value=args.worlds).start() as bar:
            futures = []
            for i in range(args.worlds):
                futures.append(pool.submit(spawn_container, i, bar, no_bag, args))
            pool.shutdown(wait=True)
            bar.finish()
    except Exception as e:
        print(e)
        pool.shutdown(wait=False)
        sp.Popen("docker kill $(docker ps -q)", shell=True)
        return
    finally:
        purge_worlds()

def check_positive(value):
    try:
        value = int(value)
        if value <= 0:
            raise argparse.ArgumentTypeError("{} is not a positive integer".format(value))
    except ValueError:
        raise Exception("{} is not an integer".format(value))
    return value

def check_pose(value):
    try:
        ret_value = value.split()
        if len(ret_value) != 2:
            raise argparse.ArgumentTypeError(f"Given pose value \"{value}\" is not made of 2 numbers")
        return (float(ret_value[0]), float(ret_value[1]))
    except ValueError:
        raise Exception("{} is not made of 2 numbers".format(value))

def check_positive_float(value):
    try:
        value = float(value)
        if value <= 0:
            raise argparse.ArgumentTypeError("{} is not a positive float".format(value))
    except ValueError:
        raise Exception("{} is not a float".format(value))
    return value

def parse_args():
    parser = argparse.ArgumentParser(description='Start exploration in docker containers.')
    parser.add_argument('--map', default=os.path.join(os.getcwd(), "src/exp_cov/maps_rgb_lab/map1/map1_rgb.png"),
        help="Path to the rgb map file.", metavar="MAP_PATH")
    parser.add_argument('--mask', default=os.path.join(os.getcwd(), "src/exp_cov/maps_rgb_lab/map1/map1_movement_mask.png"),
        help="Path to the rgb mask map file for movement areas. Each rgb object in the map will be moved within the yellow mask given in this file. If the object is none, then it can move freely.", metavar="MASK_PATH")
    parser.add_argument("--worlds", type=check_positive, default=1, metavar="WORLDS",
        help="Use this to produce WORLDS world files.")    
    parser.add_argument("--workers", type=check_positive, default=1, metavar="WORKERS",
        help="Use this to use WORKERS workers.")    
    parser.add_argument("--speedup", type=check_positive, default=10, metavar="SPEEDUP",
        help="Use this to adjust stage simulation speed. Higher is faster but heavier on the CPU.") 
    parser.add_argument("--no-bag",  action='store_true', default=False,
        help="Use this to disable bag recording, default behaviour is enabled.") 
    parser.add_argument('--pose', type=check_pose, default=(0, 0), metavar="X Y",
        help="Robot pose X and Y coordinates.")
    parser.add_argument('--scale', type=check_positive_float, default=0.035888, metavar="METERS/PIXELS",
        help="Number of meters per pixel in png map.")
    parser.add_argument("--bag-all",  action='store_true', default=False,
        help="Use this to record all topics, default behaviour is disabled, i.e. only /odom /base_pose_ground_truth and /base_scan are recorded.") 
    parser.add_argument("--dir", "-d", default=os.getcwd(),
        help="Use this to set the output directory.") 
    parser.add_argument("--coverage", action='store_true', default=False,
        help="Use this to set the working mode to coverage instead of greedy frontier-based exploration. If true, you should give a pose file.") 
    parser.add_argument("--poses", default=os.path.join(os.getcwd(), "src/exp_cov/other/poses.txt"), metavar="POSE_FILE_PATH",
        help="Use this to set the poses to go to during coverage.") 
    return parser.parse_args()

if __name__ == "__main__":

    args = parse_args()
    workers = args.workers
    no_bag = args.no_bag 

    main(workers, no_bag, args)

    print("All workers finished")
