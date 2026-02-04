#! /usr/bin/env python3

import random
import requests
import time


def look_at(base_url, x, y, z):
    print(requests.post(base_url + 'look-at', data={ 'x': x, 'y': y, 'z': z }))

def move_to(base_url, x, y, z):
    print(requests.post(base_url + 'goto', data={ 'x': x, 'y': y, 'z': z }))

def screenshot(base_url):
    return requests.get(base_url + 'screenshot').content

base_url = 'http://minecraft.vm.nurd.space:8080/'

while True:
    look_at(base_url, random.randint(-100, 100), random.randint(-100, 100), random.randint(-100, 100))
    move_to(base_url, random.randint(-100, 100), random.randint(-100, 100), random.randint(-100, 100))
    print(screenshot(base_url))
    time.sleep(0.5)
