# -*- coding: utf-8 -*-
import hashlib

class Module:

    def __init__(self, queue, config):
        self.queue = queue     
        self.url_log = str(config.get("url_log", ""))

    def GetPartner(self):
        return 'crobo_en'
    
    def Install(self, events):
        events.OnCastleLogin.Bind(self.OnCastleLogin)         

    def OnCastleLogin(self, user, faction, auid):                        
        if int(user.clogins) == 1:
            url = self.url_log % (str(user.puid))
            self.queue.Push(url)              