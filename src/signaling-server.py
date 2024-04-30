import sys
import ssl
import asyncio
import logging
import websockets
from struct import *

logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.StreamHandler(sys.stdout))

clients = {}

formatstring = 'BB512s128s32s512s'

class packet:
  def __init__(self, data):
    unpacked = unpack(formatstring, data)
    self.command = name
    self.type = age

async def handle_websocket(websocket, path):
    client_id = None
    try:
        client_id = path.split('/')[1]
        print('Client {} connected'.format(client_id))

        # TODO: handle case where someone connects with a duplicate name
        clients[client_id] = websocket
        while True:
            data = await websocket.recv()
            command, messagetype, description, mid, destination_id, candidate = unpack(formatstring, data)
            description = description.split(b'\x00')[0].decode('ascii')
            mid = mid.split(b'\x00')[0].decode('ascii')
            destination_id = destination_id.split(b'\x00')[0].decode('ascii')
            candidate = candidate.split(b'\x00')[0].decode('ascii')
            print('\nClient {} \n\tcommand: {} \n\tmessagetype: {} \n\tdescription: {} \n\tmid: {} \n\tdestination_id: {} \n\tcandidate: {}'.format(client_id, command, messagetype, description, mid, destination_id, candidate))
            if(command == 0):
                print("sending list of clients")
                client_list = ""
                for k in clients.keys():
                    # don't include the sender in the list
                    if k != client_id:
                        client_list += str(k) + ", "
                await websocket.send(pack(formatstring, 0, 0, bytes(client_list, 'ascii'), b'', b'', b''))
            elif(command == 1):
                destination_websocket = clients.get(destination_id)
                if destination_websocket:
                    print('Client {} >>'.format(destination_id))
                    await destination_websocket.send(pack(formatstring, command, messagetype, bytes(description, 'ascii'), bytes(mid, 'ascii'), bytes(client_id, 'ascii'), bytes(candidate, 'ascii')))
                else:
                    print('Client {} not found'.format(destination_id))
                    await websocket.send(pack(formatstring, 2, 0, b'', b'', b'', b''))

    except Exception as e:
        print(e)

    finally:
        if client_id:
            del clients[client_id]
            print('Client {} disconnected'.format(client_id))


async def main():
    # Usage: ./server.py [[host:]port] [SSL certificate file]
    endpoint_or_port = sys.argv[1] if len(sys.argv) > 1 else "8001"
    ssl_cert = sys.argv[2] if len(sys.argv) > 2 else None

    endpoint = endpoint_or_port if ':' in endpoint_or_port else "0.0.0.0:" + endpoint_or_port

    if ssl_cert:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain(ssl_cert)
    else:
        ssl_context = None

    print('Listening on {}'.format(endpoint))
    host, port = endpoint.rsplit(':', 1)

    server = await websockets.serve(handle_websocket, host, int(port), ssl=ssl_context)
    await server.wait_closed()


if __name__ == '__main__':
    asyncio.run(main())
