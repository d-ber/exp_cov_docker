# Experience-based Coverage on Docker
This repo contains code and configurations to simulate experience-based coverage and greedy frontier-based exploration using Docker.

## Usage
First of all, build the Docker image:
```shell
$ docker build --tag 'rosnoetic:slam_toolbox' .
```

Make sure Docker has the appropriate access permissions.

The spawnContainers.py script can then be used to run the simulations.

Coverage example:
```shell
$ python spawnContainers.py --worlds 1 --workers 1 --speedup 10 --map .src/exp_cov/maps_rgb_lab/map1/map1_rgb.png --mask ./src/exp_cov/maps_rgb_lab/map1/map1_movement_mask.png  --pose "-1.0 0.0" --scale 0.035888 --coverage --poses src/exp_cov/other/poses.txt
```
Exploration example:
```shell
$ python spawnContainers.py --worlds 1 --workers 1 --speedup 10 --map .src/exp_cov/maps_rgb_lab/map1/map1_rgb.png --mask ./src/exp_cov/maps_rgb_lab/map1/map1_movement_mask.png  --pose "-1.0 0.0" --scale 0.035888
```

To better understand the command line arguments given to the script, check the help:
```shell
$ python spawnContainers.py -h
```

### Credits
This started as a fork of https://github.com/prina404/ros-exploration-config