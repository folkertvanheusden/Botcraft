#! /usr/bin/env python3

from argparse import ArgumentParser
from getpass import getpass

import asyncio
import controller
import io
import logging
import slixmpp
import time

from typing import Optional


class MinecraftXMPPBot(slixmpp.ClientXMPP):
    def __init__(self, jid, password):
        slixmpp.ClientXMPP.__init__(self, jid, password)

        self.domain = jid[jid.find('@') + 1:]

        self.add_event_handler('session_start', self.start)
        self.add_event_handler('message', self.message)
        self.add_event_handler('disconnected', self.disconnected)
        #self.add_event_handler('connection_failed', self.connection_failed)


    async def disconnected(self, event):
        await self.reconnect()


    #async def connection_failed(self, event):
    #    await self.reconnect()


    async def start(self, event):
        self.send_presence()
        await self.get_roster()


    async def upload_screenshot(self, content):
        print('Uploading content...')

        try:
            upload_file = self['xep_0363'].upload_file
#            if not self['xep_0454']:
#                print(
#                    'The xep_0454 module isn\'t available. '
#                    'Ensure you have \'cryptography\' '
#                    'from extras_require installed.',
#                    file=sys.stderr,
#                )
#            else:
#                upload_file = self['xep_0454'].upload_file
            return await upload_file('test.png', input_file=io.BytesIO(content), domain=self.domain, timeout=10)

        except IqTimeout:
            raise TimeoutError('Could not send message in time')

        return None


    async def message(self, msg):
        print(dir(msg))
        try:
            body = msg['body']
    #        if msg['nick'] != self.nick:
            if True:
                line = body.strip().replace('\r', '\n')
                lf = line.find('\n') 
                if lf != -1:
                    line = line[0:lf]
                line = line.replace(',', ' ')
                line = line.replace('  ', ' ')
                parts = line.split()
                if len(parts) < 1:
                    return

                cmd = parts[0].lower()
                if cmd in ('help', '!help', '#help'):
                        msg.reply('goto x y z\nlookat x y z\nstate\nscreenshot\ndig x y z\ninteract x y z\nrotate angle\nrelative-move dx dy dz').send()

                elif cmd in ('goto', 'go-to', 'go_to', 'moveto', 'move-to', 'move_to', 'move', 'position'):
                    if len(parts) == 4:
                        controller.move_to(float(parts[1]), float(parts[2]), float(parts[3]))
                        msg.reply('ok').send()
                    else:
                        print('x, y or z missing for goto')

                elif cmd in ('relative-move', 'relativemove', 'relmove', 'rel-move'):
                    if len(parts) == 4:
                        controller.relative_move(float(parts[1]), float(parts[2]), float(parts[3]))
                        msg.reply('ok').send()
                    else:
                        print('x, y or z missing for relmove')

                elif cmd in ('look-at', 'lookat', 'look_at'):
                    if len(parts) == 4:
                        controller.look_at(float(parts[1]), float(parts[2]), float(parts[3]))
                        msg.reply('ok').send()
                    else:
                        print('x, y or z missing for look-at')

                elif cmd in ('rotate', ):
                    if len(parts) == 2:
                        controller.rotate(float(parts[1]))
                        msg.reply('ok').send()
                    else:
                        print('angle missing for rotate')

                elif cmd == 'state':
                    s = controller.state()
                    msg.reply("\n".join([f'{key}: {str(s[key])}' for key in s])).send()

                elif cmd == 'dig':
                    if len(parts) == 4:
                        controller.dig(float(parts[1]), float(parts[2]), float(parts[3]))
                        msg.reply('ok').send()
                    else:
                        print('x, y or z missing for dig')

                elif cmd == 'interact':
                    if len(parts) == 4:
                        controller.interact(float(parts[1]), float(parts[2]), float(parts[3]))
                        msg.reply('ok').send()
                    else:
                        print('x, y or z missing for interact')

                elif cmd == 'screenshot':
                    if len(parts) == 1:
                        png = controller.screenshot()
                        url = await self.upload_screenshot(png)
                        html = f'<body xmlns="http://www.w3.org/1999/xhtml"><a href="{url}">{url}</a></body>'
                        message = self.make_message(mto=msg['from'].bare, mbody=url, mhtml=html)
                        message['oob']['url'] = url
                        message.send()
                    else:
                        print('no parameter required for this command')

        except Exception as e:
            print(e)


if __name__ == '__main__':
    # Setup the command line arguments.
    parser = ArgumentParser()

    # Output verbosity options.
    parser.add_argument("-q", "--quiet", help="set logging to ERROR",
                        action="store_const", dest="loglevel",
                        const=logging.ERROR, default=logging.INFO)
    parser.add_argument("-d", "--debug", help="set logging to DEBUG",
                        action="store_const", dest="loglevel",
                        const=logging.DEBUG, default=logging.INFO)

    # JID and password options.
    parser.add_argument("-j", "--jid", dest="jid",
                        help="JID to use")
    parser.add_argument("-p", "--password", dest="password",
                        help="password to use")
    parser.add_argument("-r", "--room", dest="room",
                        help="MUC room to join")
    parser.add_argument("-n", "--nick", dest="nick",
                        help="MUC nickname")

    args = parser.parse_args()

    # Setup logging.
    logging.basicConfig(level=args.loglevel,
                        format='%(levelname)-8s %(message)s')

    # Setup the MUCBot and register plugins. Note that while plugins may
    # have interdependencies, the order in which you register them does
    # not matter.
    xmpp = MinecraftXMPPBot(args.jid, args.password)
    xmpp.register_plugin('xep_0030') # Service Discovery
    xmpp.register_plugin('xep_0004') # Data Forms
    xmpp.register_plugin('xep_0060') # PubSub
    xmpp.register_plugin('xep_0199') # XMPP Ping

    xmpp.register_plugin('xep_0066')
    xmpp.register_plugin('xep_0071')
    xmpp.register_plugin('xep_0128')
    xmpp.register_plugin('xep_0363')
    try:
        xmpp.register_plugin('xep_0454')
    except slixmpp.plugins.base.PluginNotFound:
        log.error(
            'Could not load xep_0454. '
            'Ensure you have \'cryptography\' from extras_require installed.'
        )

    # Connect to the XMPP server and start processing XMPP stanzas.
    while True:
        try:
            if xmpp.connect():
                asyncio.get_event_loop().run_forever()
            else:
                print('Cannot connect')
        except Exception as e:
            print(f'main: {e}')

        time.sleep(1)
