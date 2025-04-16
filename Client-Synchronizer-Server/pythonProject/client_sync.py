from flask import Flask, request, jsonify
import json
from datetime import datetime as dt
import requests
import threading
import hashlib
from logging.handlers import TimedRotatingFileHandler

import logging
import sys
import time


from requests import session
from transliterate import translit

logger = logging.getLogger()
logger.setLevel(logging.INFO)
formatter = logging.Formatter('%(asctime)s | %(levelname)s | %(message)s')

stdout_handler = logging.StreamHandler(sys.stdout)
stdout_handler.setLevel(logging.DEBUG)
stdout_handler.setFormatter(formatter)

file_handler = TimedRotatingFileHandler('sync_logs/sync_log.txt', when='midnight', interval=1, backupCount=7, encoding='utf-8')
#file_handler = logging.FileHandler('sync_log.txt',encoding='utf-8')
file_handler.setLevel(logging.DEBUG)
file_handler.setFormatter(formatter)


logger.addHandler(file_handler)
logger.addHandler(stdout_handler)

api_key = open('api_key.txt').readline()

timeToKillPublicSession = 7200 # 2 hours
timeToKillTestSession = 30 # test session - 30 seconds

timeToKillRottenSession = 70 # 1 minute + 10 seconds

lastTimeCheck = dt.now()

activeSessionTokens = {}

webSessions = {}

app = Flask(__name__)

def killOldSessions(sessionDict):
    for oldSessionToken in list(sessionDict.keys()):
        oldSession = sessionDict[oldSessionToken]
        targetTimeDifferenceToKill = timeToKillPublicSession
        if oldSessionToken == 'Tester00Tester00Tester00Tester00':
            targetTimeDifferenceToKill = timeToKillTestSession
        if (dt.now() - oldSession['timestamp']).total_seconds() > targetTimeDifferenceToKill:
            del sessionDict[oldSessionToken] # remove old sessions
            
            
def sendSessionFinishData(data):
    def sendInfo(data):
        infoSend = False
        while not infoSend:
            try:
                requests.post('https://pw2.26rus-game.ru/api/launcher/', json={'method': 'finishGame', 'data': data});
                infoSend = True
            except:
                logger.info('No backend response!!!      ' + str(json.dumps(data)))
                time.sleep(10)
    
    thread = threading.Thread(target=sendInfo, args=(data,))
    thread.start()

@app.route('/api', methods=['POST'])
def api():
    print(request.data)
    logger.info(request.data)
    #reqData = request.data.decode(encoding='cp1251', errors='ignore')
    reqData = request.data.decode(encoding='utf-8', errors='ignore')
    if request.method == 'POST':
        reqJson = json.loads(reqData, strict=False)
        method = str(reqJson['method'])
        data = 0
        if 'data' in reqJson:
            data = reqJson['data']
        if 'body' in reqJson:
            data = reqJson['body']

        # Register session (from web-launcher)
        if method == 'registerSession':
            killOldSessions(webSessions)

            key = reqJson['key']
            if key != api_key:
                response = {
                    'error': 'Invalid API key'
                }
                return jsonify(response)

            if data['sessionToken'] in webSessions:
                response = {
                    'error': 'Session already registered'
                }
                return jsonify(response)

            if not data['players']:
                response = {
                    'error': 'Failed to get players list'
                }
                return jsonify(response)

            webSessions[data['sessionToken']] = {'players': {}, 'lock': threading.Lock(), 'timestamp': dt.now(), 'gameName': '', 'gameStarted': False, 'gameFinished': False, 'gameCreated': False }

            # generate player keys, fill player data
            for player in data['players']:
                if 'id' not in player:
                    response = {
                        'error': 'Invalid player detected (no id) in session ' + data['sessionToken']
                    }
                    return jsonify(response)
                
                #player['nickname'] = translit(player['nickname'], 'ru', reversed=True).replace("'", "")
                m = hashlib.sha256((str(player['id']) + data['sessionToken'] + api_key).encode('utf-8'))
                webSessions[data['sessionToken']]['players'][m.hexdigest()] = player
                logger.info(m.hexdigest())

            response = {
                'error': ''
            }
            return jsonify(response)
            
        if method == 'createWebSession':
            if 'sessionToken' not in data or 'apiKey' not in data or 'create' not in data:
                response = {
                    'error': 'Invalid request'
                }
                logger.info('Response!!!      ' + method + ': ' + str(json.dumps(response)))
                return jsonify(response)
            
            sessionToken = data['sessionToken']
            apiKey = data['apiKey']
            
            if apiKey != api_key:
                response = {
                    'error': 'Invalid api key'
                }
                logger.info('Response!!!      ' + method + ': ' + str(json.dumps(response)))
                return jsonify(response)
            
            if sessionToken not in webSessions:
                response = {
                    'error': 'Session token was not found in active sessions'
                }
                logger.info('Response!!!      ' + method + ': ' + str(json.dumps(response)))
                return jsonify(response)
                
            with webSessions[sessionToken]['lock']:
                if data['create']:
                    if webSessions[sessionToken]['gameStarted']:
                        response = {
                            'error': 'Game has been already created'
                        }
                        logger.info('Response!!!      ' + method + ': ' + str(json.dumps(response)))
                        return jsonify(response)
                    else:
                        webSessions[sessionToken]['gameStarted'] = True
                    
                mapId = 'Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb';
                if 'mapId' in webSessions[sessionToken]:
                    mapId = webSessions[sessionToken]['mapId'];
                    
                response = {
                    'error': '',
                    'usersData': list(webSessions[sessionToken]['players'].values()),
                    'mapId': mapId
                }
                logger.info('Response!!!      ' + method + ': ' + str(json.dumps(response)))
                return jsonify(response)
            
            

        # Connect player to web-session
        if method == 'connectToWebSession':
            if 'sessionToken' not in data or 'playerKey' not in data:
                response = {
                    'error': 'Invalid request'
                }
                return jsonify(response)

            sessionToken = data['sessionToken']
            playerKey = data['playerKey']

            if sessionToken not in webSessions:
                response = {
                    'error': 'Session token was not found in active sessions'
                }
                return jsonify(response)

            if playerKey not in webSessions[sessionToken]['players']:
                response = {
                    'error': 'Invalid player key'
                }
                return jsonify(response)

            # critical section for specific session!
            with webSessions[sessionToken]['lock']:
                method = ''
                tryRotten = False
                if not webSessions[sessionToken]['gameName']:
                    webSessions[sessionToken]['gameName'] = webSessions[sessionToken]['players'][playerKey]['nickname']
                    method = 'create'
                else:
                    if webSessions[sessionToken]['gameStarted']:
                        method = 'reconnect'
                    else:
                        method = 'connect'
                        tryRotten = True
                     
                # Test rotten sessions
                if tryRotten:
                    rotSession = webSessions[sessionToken]
                    targetTimeDifferenceToKill = timeToKillRottenSession
                    if (dt.now() - rotSession['timestamp']).total_seconds() > targetTimeDifferenceToKill:
                        rotKillerData = {"sessionToken":sessionToken,"win":0,"afk":[]}
                        sendSessionFinishData(rotKillerData)
                        response = {
                            'error': 'Rotten session'
                        }
                        logger.info('Response!!!      ' + str(json.dumps(response)))
                        return jsonify(response)
                    
                gameName = ''
                if webSessions[sessionToken]['gameCreated']:
                    gameName = webSessions[sessionToken]['gameName']

                response = {
                    'error': '',
                    'method': method,
                    'playerInfo': webSessions[sessionToken]['players'][playerKey],
                    'usersData': list(webSessions[sessionToken]['players'].values()),
                    'gameName': gameName
                }
                logger.info('Response!!!      ' + str(json.dumps(response)))
                return jsonify(response)

        # Notify game start event
        if method == 'notifyGameStart':
            if 'sessionToken' not in data:
                response = {
                    'error': 'Invalid request'
                }
                return jsonify(response)
            sessionToken = data['sessionToken']

            with webSessions[sessionToken]['lock']:
                webSessions[sessionToken]['gameStarted'] = True
                response = {
                    'error': '',
                }
                return jsonify(response)

        if method == 'notifyGameFinish':
            if 'sessionToken' not in data:
                response = {
                    'error': 'Invalid request',
                }
                return jsonify(response)
                
            if 'afk' in data and not data['afk']:
                data['afk'] = []
                
            sessionToken = data['sessionToken']
            
            if sessionToken not in webSessions:
                # synchronizer restarted, just send results
                webSessions[sessionToken] = {'players': {}, 'lock': threading.Lock(), 'timestamp': dt.now(), 'gameName': '', 'gameStarted': False, 'gameFinished': True, 'gameCreated': False }
                with webSessions[sessionToken]['lock']:
                    sendSessionFinishData(data)
                    response = {
                        'error': '',
                    }
                    logger.info('Response!!!      ' + str(json.dumps(response)))
                    return jsonify(response)

            with webSessions[sessionToken]['lock']:
                if webSessions[sessionToken]['gameFinished']:
                    response = {
                        'error': 'Game finished'
                    }
                    return jsonify(response)
                else:
                    webSessions[sessionToken]['gameFinished'] = True
                    sendSessionFinishData(data)
                    response = {
                        'error': '',
                    }
                    logger.info('Response!!!      ' + str(json.dumps(response)))
                    return jsonify(response)
                    
        if method == 'notifyGameFinishLegacy':
            response = {
                'error': '',
            }
            return jsonify(response)

        # Get game name if is in connection process
        if method == 'getGameNameForConnection':
            if 'sessionToken' not in data:
                response = {
                    'error': 'Invalid request'
                }
                return jsonify(response)
            sessionToken = data['sessionToken']

            with webSessions[sessionToken]['lock']:
                if webSessions[sessionToken]['gameStarted']:
                    response = {
                        'error': 'reconnect',
                        'data': {'gameName': webSessions[sessionToken]['gameName']}
                    }
                    logger.info('Response!!!      ' + str(json.dumps(response)))
                    return jsonify(response)
                    
                gameName = ''
                if webSessions[sessionToken]['gameCreated']:
                    gameName = webSessions[sessionToken]['gameName']
                    
                response = {
                    'error': '',
                    'data': {'gameName': gameName}
                }
                logger.info('Response!!!      ' + str(json.dumps(response)))
                return jsonify(response)

        # Web-Lobby created by player
        if method == 'webLobbyCreated':
            if 'sessionToken' not in data:
                response = {
                    'error': 'Invalid request'
                }
                return jsonify(response)
            sessionToken = data['sessionToken']
            if sessionToken in webSessions:
                with webSessions[sessionToken]['lock']:
                    webSessions[sessionToken]['gameCreated'] = True
                    webSession = webSessions[sessionToken]
                    
                    if webSession['gameName'] == data['nickname']:  # save gameId
                        response = {
                            'error': ''
                        }
                        return jsonify(response)
                    else:
                        return 'Game already registered by someone else'
            else:
                return 'Invalid session id'

        # Check connection (checkInstall)
        if method == 'checkConnection':
            response = {
                'error': '',
                'data': True
            }
            return jsonify(response)


        return 'Unknown method in json'
    else:
        return 'Request method is not allowed'
