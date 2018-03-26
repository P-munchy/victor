import websocket
import json
import threading
import logging
from statistics import mode, mean 

class VictorWebsocket(object):
  def __init__(self, robot_ip):
    self.logger = self.logging()

    websocket.enableTrace(True)
    self.robot_ip = robot_ip
    self.subscribeData = {"type": "subscribe", "module": None}
    
    self.micdata_port = 8889
    #TODO: Make this a blocking action, until a websocket connection is esablished
    self.mic_socket_established = False
    self.mic_stream_flag = False
    
    self.mic_data_raw_results = {"dominant": [], "confidence": [0]*12}
    self.init_micdata_socket()

  def init_micdata_socket(self):
    mic_websocket_url = "ws://{0}:{1}/socket".format(self.robot_ip, self.micdata_port)
    
    self.ws_micdata = websocket.WebSocketApp(mic_websocket_url,
                                    on_message = self.on_mic_message,
                                    on_error = self.on_error,
                                    on_close = self.on_close)
    
    self.ws_micdata.on_open = self.on_open_mic
    self.ws_micdata.keep_running = True 

    #Seperate background thread, to handle incoming messages
    self.wst_micdata = threading.Thread(target=self.ws_micdata.run_forever)
    self.wst_micdata.daemon = True 
    self.wst_micdata.start()
    
  def close_mic_socket(self):
    self.ws_micdata.keep_running = False
    return

  def on_open_mic(self, ws):
    self.subscribeData['module'] = 'micdata'
    subscribe_message = json.dumps(self.subscribeData)
    self.logger.info("Now waiting for a reply")
    ws.send(subscribe_message)
    self.mic_socket_established = True

  def on_mic_message(self, ws, message):
    if self.mic_stream_flag == True:
      self.process_micdata_stream(message)
  
  def process_micdata_stream(self, message):
    json_data = json.loads(message)
    dominant = json_data["data"]["dominant"]
    confidence = json_data["data"]["confidence"]

    if dominant >= 12: 
        return
    else:
        self.mic_data_raw_results['dominant'].append(dominant)
        self.mic_data_raw_results['confidence'][dominant] += confidence
  
  def compile_mic_results(self):
    #Find mode of the 12 audio directions
    dominant_list = self.mic_data_raw_results['dominant']
    confidence_list = self.mic_data_raw_results['confidence']
    final_direction = mode(dominant_list)
    
    #Calculate the average confidence for that direction
    final_direction_confidence = confidence_list[final_direction]
    final_direction_samples = len(dominant_list)
    final_confidence_avg = final_direction_confidence / final_direction_samples
    
    #reset raw results
    self.mic_data_raw_results['dominant'].clear()
    self.mic_data_raw_results['confidence'] = [0] * 12 
    
    results_dict = {"final_dominant": final_direction,
                    "final_confidence": final_confidence_avg}

    return results_dict

  def on_error(self, ws, error):
    self.logger.info(error)

  def on_close(ws):
    self.logger.info("### closed ###")
  
  def logging(self):
    logger = logging.getLogger(__name__)
    logger.setLevel(logging.DEBUG)
    console = logging.StreamHandler() #for now, print to just console
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    console.setFormatter(formatter)
    logger.addHandler(console)
    return logger
