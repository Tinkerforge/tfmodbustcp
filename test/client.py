import time
import socket
import random

connections = []

def check(connection):
    try:
        if len(connection.recv(1)) == 0:
            print(f'connection {connection} disconnected by peer')
            connection.close()
            return False
    except TimeoutError:
        pass
    except Exception as e:
        print(f'connection {connection} check failed: {e}')
        connection.close()
        return False

    return True

while True:
    print(f'connection count: {len(connections)}')
    time.sleep(0.5)

    if random.randint(0, 100) < 75:
        try:
            print('connecting...')
            connection = socket.create_connection(('localhost', 502))
            print(f'connected: {connection}')
        except Exception as e:
            print(f'connect failed: {e}')
            continue

        connection.settimeout(0.01)
        connections.append(connection)

    connections = [connection for connection in connections if check(connection)]

    if random.randint(0, 100) < 25:
        if len(connections) > 1:
            index = random.randrange(len(connections) - 1)
            connection = connections.pop(index)
            print(f'disconnecting: {connection}')
            connection.close()
