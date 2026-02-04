import random
import requests
import time


base_url = 'http://minecraft.vm.nurd.space:8080/'

def state():
    return requests.get(base_url + 'state').json()

def dig(x, y, z):
    print(requests.post(base_url + 'dig', data={ 'x': x, 'y': y, 'z': z }))

def interact(x, y, z):
    print(requests.post(base_url + 'interact', data={ 'x': x, 'y': y, 'z': z }))

def look_at(x, y, z):
    print(requests.post(base_url + 'look-at', data={ 'x': x, 'y': y, 'z': z }))

def move_to(x, y, z):
    print(requests.post(base_url + 'goto', data={ 'x': x, 'y': y, 'z': z }))

def relative_move(dx, dy, dz):
    s = state()
    move_to(s['x'] + dx, s['y'] + dy, s['z'] + dz)

def screenshot():
    return requests.get(base_url + 'screenshot').content

def set_url(new_base_url):
    global base_url
    base_url = new_base_url


if __name__ == '__main__':
    base_url = 'http://minecraft.vm.nurd.space:8080/'

    while True:
        look_at(random.randint(-100, 100), random.randint(-100, 100), random.randint(-100, 100))
        move_to(random.randint(-100, 100), random.randint(-100, 100), random.randint(-100, 100))
        #print(screenshot(base_url))
        print(state())
        time.sleep(0.5)
