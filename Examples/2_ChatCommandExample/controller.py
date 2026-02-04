#! /usr/bin/env python3

import random
import requests
import time


def look_at(base_url, x, y, z):
    print(requests.post(base_url + 'look-at', data={ 'x': x, 'y': y, 'z': z }))

def move_to(base_url, x, y, z):
    print(requests.post(base_url + 'goto', data={ 'x': x, 'y': y, 'z': z }))

while True:
    look_at('http://minecraft.vm.nurd.space:8080/', random.randint(-100, 100), random.randint(-100, 100), random.randint(-100, 100))
    move_to('http://minecraft.vm.nurd.space:8080/', random.randint(-100, 100), random.randint(-100, 100), random.randint(-100, 100))
    time.sleep(0.5)
