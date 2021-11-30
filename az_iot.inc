///////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Azure IoT Hub functions
//
///////////////////////////////////////////////////////////////////////////////////////////////////////
#IFDEF NOT __AZ_IOT_INC__ THEN
#DEFINE __AZ_IOT_INC__


#DEFINE AZI_MESSAGE_TYPE_UNKNOWN      -1
#DEFINE AZI_MESSAGE_TYPE_C2D_MSG       1
#DEFINE AZI_MESSAGE_TYPE_METHOD        2
#DEFINE AZI_MESSAGE_TYPE_TWIN_RESPONSE 3
#DEFINE AZI_MESSAGE_TYPE_TWIN_REPORT   4
#DEFINE AZI_MESSAGE_TYPE_TWIN_CHANGED  5

VAR
      
      
   _azi_private_port                : INT := 8883;
   _azi_private_keepalive           : INT := 120;
   
   _azi_private_prop_topic          : STRING;
   _azi_private_prop_key            : ARRAY[0..9]OF STRING;
   _azi_private_prop_val            : ARRAY[0..9]OF STRING;
   _azi_private_prop_count          : INT := 0;

   _azi_private_dm_topic            : STRING;
   _azi_private_dm_method           : STRING;
   _azi_private_dm_rid              : STRING;
   _azi_private_dm_active           : BOOL := FALSE;
   
   _azi_private_tw_topic            : STRING :="$$iothub/twin/";

   _azi_private_tw_req_rid          : STRING;
   _azi_private_tw_req_status       : INT := -1;
   
   _azi_private_tw_set_rid          : STRING;
   _azi_private_tw_set_status       : INT;
   
   _azi_private_tw_desired_topic    : STRING := "$$iothub/twin/PATCH/properties/desired/";
   _azi_private_tw_desired_version  : STRING;
   
   _azi_private_deviceId            : STRING;
   _azi_private_mqtt_rxd            : mqttReceive;
   _azi_private_mqtt_handle         : INT := -1;
END_VAR;

FUNCTION _azi_private_ParseProperties:INT;
VAR_INPUT
   str:STRING;
   ignore_sys:BOOL:=TRUE;
END_VAR;
VAR
   i:INT;
   tmp:STRING;
   ch:STRING;
   val:STRING;
   ch_val:SINT;
   ignore_prop:BOOL:=FALSE;
   reading_key : BOOL := TRUE;
END_VAR;
   i:=1;
   _azi_private_prop_count := 0;
   while i <= strLen(str:=str) DO
      ch := strMid(str:=str, start:=i,length:=1) ;
      IF(ch = "%") THEN
         IF(i+2<=strLen(str:=str) ) THEN
            val:= strMid(str:=str, start:=i+1,length:=2) ;
            i:=i+2;
            IF(val = "24" AND tmp = "" AND reading_key) THEN// first in name is $, system property  
               IF(ignore_sys) THEN
                  ignore_prop := TRUE;// Do not store system property
               ELSE
                  tmp := tmp + "$$";
               END_IF;
            ELSE
               ch_val := hexToSint(hex:=val);
               tmp := tmp + strFromMemory(src:=ADDR(ch_val), len:=1);
            END_IF;
         ELSE// Read beyond the end of the string
            i:=i+2;
         END_IF;
      ELSIF (ch = "=") THEN// property val
         IF(not ignore_prop) THEN
            _azi_private_prop_key[_azi_private_prop_count] := tmp;
            _azi_private_prop_count := _azi_private_prop_count + 1;
         END_IF;
         reading_key :=FALSE;
         tmp := "";
      ELSIF (ch = "&") THEN // New property
         IF(not ignore_prop) THEN
            IF(_azi_private_prop_count>0) THEN
               _azi_private_prop_val[_azi_private_prop_count-1] := tmp;
            END_IF;
         END_IF;
         ignore_prop := FALSE;
         reading_key :=TRUE;
         tmp := "";
      ELSE
         tmp :=  tmp + ch;
      END_IF;
      i:=i+1;
   END_WHILE;
   IF(strLen(str:=tmp) > 0) THEN
      IF(not reading_key) THEN
         IF(not ignore_prop) THEN
            IF(_azi_private_prop_count>0) THEN
               _azi_private_prop_val[_azi_private_prop_count-1] := tmp;
            END_IF;
         END_IF;
      END_IF;
   
   END_IF;
   
END_FUNCTION;

FUNCTION _azi_private_ParseMethod:INT;
VAR_INPUT
   str:STRING;
END_VAR;
VAR
   i:INT;
   stop: INT := 0;
   len : INT := 999;
END_VAR;
   
   _azi_private_dm_method := strToken(str:=str, delimiter:="/", index:=1);
   i := strFind(str1:=str, str2:="?$$rid=");
   IF(i > 0) THEN
      stop := strFind(str1:=str, str2:="&", start:=i+6);
      IF(stop > 0) THEN
         len := stop-(i+6);
      END_IF;
      _azi_private_dm_rid := strMid(str:=str, start:=i+6, length := len);
   END_IF;
   _azi_private_dm_active := TRUE;
END_FUNCTION;

FUNCTION _azi_private_ParseTwin:INT;
VAR_INPUT
   str:STRING;
END_VAR;
VAR
   i:INT;
   stop: INT := 0;
   len : INT := 999;
   start:STRING := "?$$rid=";
   rid:STRING;
END_VAR;
   _azi_private_ParseTwin := -1;
   i := strFind(str1:=str, str2:=start);
   IF(i > 0) THEN
      stop := strFind(str1:=str, str2:="&", start:=i+6);
      IF(stop > 0) THEN
         len := stop-(i+6);
      END_IF;
      rid := strMid(str:=str, start:=i+6, length := len);
      IF(rid = _azi_private_tw_req_rid) THEN
         _azi_private_tw_req_status := strToInt(str:=strToken(str:=str, delimiter:="/", index := 1));
         _azi_private_ParseTwin := AZI_MESSAGE_TYPE_TWIN_RESPONSE;
      END_IF;
      IF (rid = _azi_private_tw_set_rid) THEN
         _azi_private_tw_set_status := strToInt(str:=strToken(str:=str, delimiter:="/", index := 1));
         _azi_private_ParseTwin := AZI_MESSAGE_TYPE_TWIN_REPORT;
      END_IF;
   END_IF;
   
   
END_FUNCTION;

FUNCTION _azi_private_ParseTwinDesired:INT;
VAR_INPUT
   str:STRING;
END_VAR;
VAR
   i:INT;
   start:STRING := "?$$version=";
   version:STRING;
END_VAR;
   _azi_private_ParseTwinDesired := -1;
   i := strFind(str1:=str, str2:=start);
   IF(i > 0) THEN
      _azi_private_tw_desired_version := strMid(str:=str, start:=i+10);
      _azi_private_ParseTwinDesired := 0;
   END_IF;
END_FUNCTION;

FUNCTION _azi_private_SetupVars: INT;
VAR_INPUT
   deviceId:STRING;
   password:STRING;
   server : STRING;
   o_username: ACCESS STRING;
END_VAR;
   _azi_private_deviceId := deviceId;
   o_username := server+"/"+_azi_private_deviceId+"/api-version=2016-11-14";

   _azi_private_prop_topic := "devices/"+_azi_private_deviceId+"/messages/devicebound/";
   _azi_private_dm_topic := "$$iothub/methods/POST/";
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// iotOpenSAS()
//
// Description:
//    Open the connection to the IoT hub, using security tokens, in the format 
//    SharedAccessSignature sig={signature-string}&se={expiry}&skn={policyName}&sr={URL-encoded-resourceURI}
//
// Input:
//    deviceId    - The device identifier
//    password    - The security token.
//    server      - The hostname of the IoT hub server.
//    iface       - The network interface to use.
//
// Returns:
//    0          - Success.
//    -1         - No available connections.
//    -2         - Invalid parameter
//    -3         - Connection is already in use.
//
FUNCTION ALIGN aziOpenSAS : INT;
VAR_INPUT
   deviceId:STRING;
   password:STRING;
   server : STRING;
   iface: SINT;
END_VAR;
VAR
   username : STRING;
END_VAR;
   IF _azi_private_mqtt_handle >= 0 THEN
      aziOpenSAS := -3;
      RETURN;
   END_IF;
   _azi_private_SetupVars(deviceId:=deviceId, password:=password, server:=server, o_username:=username);

   _azi_private_mqtt_handle  := mqttOpen(ip := server, port := _azi_private_port, clientid := _azi_private_deviceId, iface:=iface, useTLS:=TRUE, 
   username := username, password := password, keepalive := _azi_private_keepalive, clean := TRUE);
   IF(_azi_private_mqtt_handle < 0) THEN
      aziOpenSAS:=_azi_private_mqtt_handle;
   ELSE
      aziOpenSAS:=0;
   END_IF;
   
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziOpenCert()
//
// Description:
//    Open the connection to the IoT hub, using X.509 certificates
//
// Input:
//    deviceId    - The device identifier
//    cert        - The name of the client certificate
//    cert_pass   - The password for the private key in the client certificate
//    server      - The hostname of the IoT hub server.
//    iface       - The network interface to use.
//
// Returns:
//    0          - Success.
//    -1         - No available connections.
//    -2         - Invalid parameter
//    -3         - Connection is already in use.
//
FUNCTION ALIGN aziOpenCert : INT;
VAR_INPUT
   deviceId:STRING;
   cert:STRING;
   cert_pass:STRING;
   server : STRING;
   iface: SINT;
END_VAR;
VAR
   username : STRING;
END_VAR;
   IF _azi_private_mqtt_handle >= 0 THEN
      aziOpenCert := -3;
      RETURN;
   END_IF;
   
   _azi_private_SetupVars(deviceId:=deviceId, password:="", server:=server, o_username:=username);

   _azi_private_mqtt_handle  := mqttOpen(ip := server, port := _azi_private_port, clientid := _azi_private_deviceId, iface:=iface, useTLS:=TRUE, tls_certificate := cert, 
   tls_password:=cert_pass, username := username, keepalive := _azi_private_keepalive, clean := TRUE);
   IF(_azi_private_mqtt_handle < 0) THEN
      aziOpenCert:=_azi_private_mqtt_handle;
   ELSE
      aziOpenCert:=0;
   END_IF;
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziClose()
//
// Description:
//    Close the connection to the IoT hub
//
// Input:
//    None.
//
// Returns:
//    None.
//
FUNCTION ALIGN aziClose;
   IF(_azi_private_mqtt_handle >= 0) THEN
      mqttClose(handle:=_azi_private_mqtt_handle);
   END_IF;
   _azi_private_mqtt_handle := -1;
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziSubscribe()
//
// Description:
//    Subscribe to the events on the connected IoT hub.
//
// Input:
//    None.
//
// Returns:
//    0         - Success.
//    1         - Invalid connection.
//    2         - Not connected to server.
//    3         - Invalid parameter
//
FUNCTION ALIGN aziSubscribe : INT;
   IF(_azi_private_mqtt_handle < 0) THEN
      aziSubscribe := 1;
      RETURN;
   END_IF;
   aziSubscribe := mqttSubscribe(handle := _azi_private_mqtt_handle, qos := 2, topic := _azi_private_prop_topic+"#");
   IF(aziSubscribe <> 0) THEN 
      RETURN;
   END_IF;
   
   aziSubscribe := mqttSubscribe(handle := _azi_private_mqtt_handle, qos := 2, topic := _azi_private_dm_topic+"#");
   IF(aziSubscribe <> 0) THEN 
      RETURN;
   END_IF;
   
   aziSubscribe := mqttSubscribe(handle := _azi_private_mqtt_handle, qos := 2, topic := _azi_private_tw_topic+"res/#");
   IF(aziSubscribe <> 0) THEN 
      RETURN;
   END_IF;
   
   aziSubscribe := mqttSubscribe(handle := _azi_private_mqtt_handle, qos := 2, topic := _azi_private_tw_desired_topic+"#");
   IF(aziSubscribe <> 0) THEN 
      RETURN;
   END_IF;
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziConnected()
//
// Description:
//    Tells if the connection to the IoT hub is established.
//
// Input:
//    None.
//
// Returns:
//    TRUE      - Device is connected to IoT hub.
//    FALSE     - Device is not connected to IoT hub.
//
FUNCTION ALIGN aziConnected : BOOL;
   aziConnected := mqttConnected(handle := _azi_private_mqtt_handle);
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziStatus()
//
// Description:
//    Returns the status of the connection.
//
// Input:
//    None.
//
// Returns:
//    0         - Connected to the IoT Hub.
//    1         - Invalid connection.
//    2         - Not connected to IoT hub.
//    3         - IoT hub server not found.
//    4         - No reply from IoT hub
//    5         - Connection rejected, unacceptable protocol version.
//    6         - Connection rejected, client ID rejected.
//    7         - Connection rejected, server unavailable.
//    8         - Connection rejected, bad user-name or password.
//    9         - Connection rejected, not authorized.
//    20        - Secure connection failed.
//    21        - Secure connection require client certificate.
//    22        - Certificate verifications failed.
//
FUNCTION ALIGN aziStatus : SINT;
   aziStatus := mqttStatus(handle := _azi_private_mqtt_handle);
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziWaitEvent()
//
// Description:
//    Waits for a new message, blocking the thread.
//
// Input:
//    timeout   - The number of seconds to wait before timing out. Use 0 to return immediately, use -1 to wait forever.
//
// Returns:
//    1         - A messages is received. Use aziReceiveMessage to read it.
//    0         - Timeout.
//   -1         - No connection.
//
FUNCTION ALIGN aziWaitEvent : INT;
VAR_INPUT
   timeout   : INT;
END_VAR;
   aziWaitEvent := mqttWaitEvent(timeout := timeout);
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziReceiveMessage()
//
// Description:
//    Reads a new message.
//
// Input:
//    data      - Address of a buffer to store the data of the message in.
//    maxsize   - The size of the buffer. Any message data after this limit is lost.
//
// Output:
//    ready     - True if a message has been received.
//    size      - The size of the read data.
//    type      - The type of the message:
//               -1  - Unknown message.
//                1  - Cloud to device message. Use aziGetProperty to read the properties of the message. The data contains the contents of the message.
//                2  - Direct method request. Use aziGetMethodRequest to read the name of the requested method and use aziMethodReply to reply to the request. The data contains the parameters for the method as JSON.
//                3  - Device twin response. The data contains the entire device twin. Use aziTwinRequestStatus() to get the status.
//                4  - Device twin report property reply. Use aziTwinReportPropertyStatus() to get the status.
//                5  - Update Device Twin property. Use aziTwinDesiredVersion() to get the desired version number. The data contains JSON with the desired properties.
//
//
FUNCTION_BLOCK ALIGN aziReceiveMessage;
VAR_INPUT
   data      : PTR;
   maxsize   : INT;
END_VAR;
VAR_OUTPUT
   size      : INT;
   ready     : BOOL;
   type      : INT;
END_VAR;
VAR
   rc:INT;
END_VAR;
   rc := mqttWaitEvent(timeout := 0);
   IF rc > 0 THEN
      _azi_private_mqtt_rxd(data := (data), maxsize := maxsize);
      IF(_azi_private_mqtt_rxd.ready) THEN
         IF(_azi_private_mqtt_rxd.handle = _azi_private_mqtt_handle) THEN
            
            IF(strMid(str:=_azi_private_mqtt_rxd.topic, start:=1, length:=strLen(str:=_azi_private_prop_topic)) = _azi_private_prop_topic) THEN
              // Message with properties
               _azi_private_ParseProperties(str:=strMid(str:=_azi_private_mqtt_rxd.topic, start:=strLen(str:=_azi_private_prop_topic)+1));
               
               type := AZI_MESSAGE_TYPE_C2D_MSG;
            ELSIF (strMid(str:=_azi_private_mqtt_rxd.topic, start:=1, length:=strLen(str:=_azi_private_dm_topic)) = _azi_private_dm_topic) THEN
               // Direct method
               _azi_private_ParseMethod(str:=strMid(str:=_azi_private_mqtt_rxd.topic, start:=strLen(str:=_azi_private_dm_topic)+1));
               
               
               type := AZI_MESSAGE_TYPE_METHOD;
            ELSIF (strMid(str:=_azi_private_mqtt_rxd.topic, start:=1, length:=strLen(str:=_azi_private_tw_topic+"res/")) = (_azi_private_tw_topic+"res/")) THEN
               // Device twin
                rc := _azi_private_ParseTwin(str:=strMid(str:=_azi_private_mqtt_rxd.topic, start:=strLen(str:=_azi_private_tw_topic+"res/")+1));
               IF rc > 0 THEN
                  type := rc;
               ELSE
                  type := -1;
               END_IF;
            ELSIF (strMid(str:=_azi_private_mqtt_rxd.topic, start:=1, length:=strLen(str:=_azi_private_tw_desired_topic)) = (_azi_private_tw_desired_topic)) THEN
               // Device twin changed
                _azi_private_ParseTwinDesired(str:=strMid(str:=_azi_private_mqtt_rxd.topic, start:=strLen(str:=_azi_private_tw_desired_topic)+1));
             
               type := AZI_MESSAGE_TYPE_TWIN_CHANGED;
            ELSE
               // Unknown topic
               type := -1;
            END_IF;
            
            size := _azi_private_mqtt_rxd.size;
            
            ready := TRUE;
            RETURN;
         END_IF;
      END_IF;
   END_IF;
   ready := FALSE;
END_FUNCTION_BLOCK;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziGetProperty()
//
// Description:
//    Retrieves a property that was received with a cloud-to-device message.
//
// Input:
//    index     - The index of the property to read(1..10)
//
// Output:
//    key       - The name of the property
//    value     - The value of the property
//    
// Returns:
//    0         - Success.
//   -1         - Invalid index, there is no property at the given index.
//
FUNCTION ALIGN aziGetProperty : INT;
VAR_INPUT
   index: INT;
   key: ACCESS STRING;
   value: ACCESS STRING;
END_VAR;
   IF(index < 1 OR index > _azi_private_prop_count) THEN
      aziGetProperty := -1;
      RETURN;
   END_IF;
   key := _azi_private_prop_key[index-1];
   value:= _azi_private_prop_val[index-1];
   aziGetProperty := 0;
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziPublish()
//
// Description:
//    Sends a message to the connected IoT hub.
//
// Input:
//    properties  - The properties to send with the message. URL-encoded string.
//    data        - Address of a buffer with the data to send.
//    size        - The size of the data to send
//
// Returns:
//    0         - Success.
//    1         - Invalid connection.
//    2         - Not connected to server.
//    3         - Invalid parameter
//    4         - A publish is already in progress
//    5         - Message transmission interrupted due to connection loss. The message will be transmitted again when connection is reestablished.
//
FUNCTION ALIGN aziPublish : INT;
VAR_INPUT
   properties     : STRING;
   data      : PTR;
   size      : INT;
END_VAR;
VAR
   topic     :STRING;
END_VAR;
   topic  := "devices/"+_azi_private_deviceId+"/messages/events/";
   aziPublish := mqttPublish(
                        handle   := _azi_private_mqtt_handle,
                        topic    := topic+properties,
                        qos      := 0,
                        retained := OFF,
                        data     := data,
                        size     := size
                       );
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziGetMethodRequest()
//
// Description:
//    Retrieves the name of the requested method.
//
// Output:
//    method    - The name of the called method.
//    
// Returns:
//    0         - Success.
//   -1         - No unread method request available.
//
FUNCTION ALIGN aziGetMethodRequest:INT;
VAR_INPUT
   method:ACCESS STRING;
END_VAR;

   IF(_azi_private_dm_active) THEN
      method := _azi_private_dm_method;
      aziGetMethodRequest := 0;
   ELSE
      aziGetMethodRequest := -1;
   END_IF;

END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziMethodReply()
//
// Description:
//    Sends a reply to a method request.
//
// Input:
//    status    - The status code for the reply.
//    data      - Address of buffer with the payload for the reply.
//    size      - The size of the payload.
//    
// Returns:
//    0         - Success.
//    1         - Invalid connection.
//    2         - Not connected to server.
//    3         - Invalid parameter
//    4         - A publish is already in progress
//    5         - Message transmission interrupted due to connection loss. The message will be transmitted again when connection is re-established.
//    6         - No method available to reply to.
//
FUNCTION ALIGN aziMethodReply:INT;
VAR_INPUT
   status    : INT;
   data      : PTR;
   size      : INT;
END_VAR;
VAR
   topic     :STRING;
END_VAR;
   IF(NOT _azi_private_dm_active) THEN
      aziMethodReply := 6;
      RETURN;
   END_IF;
   topic  := strFormat(format:="$$iothub/methods/res/\1/?$$rid="+_azi_private_dm_rid, v1:=status);
   aziMethodReply := mqttPublish(
                        handle   := _azi_private_mqtt_handle,
                        topic    := topic,
                        qos      := 0,
                        retained := OFF,
                        data     := data,
                        size     := size
                       );
   _azi_private_dm_active := FALSE;
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziTwinRequest()
//
// Description:
//    Request the device twin properties from the HUB. 
//    The properties will be available as the data in a Twin response from aziReceiveMessage 
//    while the status can be read with aziTwinRequestStatus.
//
// Returns:
//    0         - Success.
//    1         - Invalid connection.
//    2         - Not connected to server.
//    3         - Invalid parameter
//    4         - A publish is already in progress
//    5         - Message transmission interrupted due to connection loss. The message will be transmitted again when connection is reestablished.
//
FUNCTION ALIGN aziTwinRequest : INT;
VAR
   topic     :STRING;
END_VAR;
   _azi_private_tw_req_rid := "123";
   topic  := strFormat(format:=_azi_private_tw_topic+"GET/?$$rid="+_azi_private_tw_req_rid );
   aziTwinRequest := mqttPublish(
                        handle   := _azi_private_mqtt_handle,
                        topic    := topic,
                        qos      := 0,
                        retained := OFF,
                        size     := 0
                       );
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziTwinRequestStatus()
//
// Description:
//    Read the status of a device twin properties request.
//
// Returns:
//              - The status
//    -1        - No status available
//
//
FUNCTION ALIGN aziTwinRequestStatus : INT;
   aziTwinRequestStatus := _azi_private_tw_req_status;
   _azi_private_tw_req_status := -1;
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziTwinReportProperty()
//
// Description:
//    Changes reported device twin properties. The status of the request can be read with aziTwinReportPropertyStatus.
//
// Input:
//    data      - Address of buffer with the JSON describing the change.
//    size      - The size of the data.
//    
// Returns:
//    0         - Success.
//    1         - Invalid connection.
//    2         - Not connected to server.
//    3         - Invalid parameter
//    4         - A publish is already in progress
//    5         - Message transmission interrupted due to connection loss. The message will be transmitted again when connection is reestablished.
//
FUNCTION ALIGN aziTwinReportProperty : INT;
VAR_INPUT
   data      : PTR;
   size      : INT;
END_VAR;
VAR
   topic     :STRING;
END_VAR;
   _azi_private_tw_set_rid := "125";
   topic  := strFormat(format:=_azi_private_tw_topic+"PATCH/properties/reported/?$$rid="+_azi_private_tw_set_rid );
   aziTwinReportProperty := mqttPublish(
                        handle   := _azi_private_mqtt_handle,
                        topic    := topic,
                        qos      := 0,
                        retained := OFF,
                        data     := data,
                        size     := size
                       );
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziTwinReportPropertyStatus()
//
// Description:
//    Read the status of a device twin report property request.
//
// Returns:
//              - The status
//    -1        - No status available
//
//
FUNCTION ALIGN aziTwinReportPropertyStatus : INT;
   aziTwinReportPropertyStatus := _azi_private_tw_set_status;
   _azi_private_tw_set_status := -1;
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziTwinDesiredVersion()
//
// Description:
//    Read the desired version number of the twin property change request received from the HUB. 
//
// Returns:
//       The desired version or an empty string if none is available.
//
FUNCTION ALIGN aziTwinDesiredVersion : STRING;
   aziTwinDesiredVersion:=_azi_private_tw_desired_version;
   _azi_private_tw_desired_version := "";
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziCreateSAS
//
// Description:
//    This function will generate a SAS token.
//
// Input:
//    lifetime          - The time in seconds the token will be valid.
//    tz_offset         - The time offset.
//    host              - The host address of the server.
//    deviceId          - The device ID.
//    sharedAccessKey   - The base64 encoded shared access key.
//
// Returns:
//    A string with the SAS token.
//
FUNCTION ALIGN aziCreateSAS:STRING;
VAR_INPUT
   lifetime          : DINT := 86400;// 24 hours
   tz_offset         : DINT := 0;
   host              : MANDATORY STRING;
   deviceId          : MANDATORY STRING;
   sharedAccessKey   : MANDATORY STRING;
   reserved          : STRING := "";
END_VAR;
VAR
   err               : INT;
END_VAR;
   aziCreateSAS := STRING(INT(MODCALL("mod_iot_hub", "aziCreateSAS", err)));
   IF err <> 0 THEN
      aziCreateSAS := "";
   END_IF;
END_FUNCTION;

///////////////////////////////////////////////////////////////////////////////////////////////////////
// aziLoadModule
//
// Description:
//    This function opens the internal drive and loads the Extension module.
//
// Input:
//    None.
//
// Returns:
//    0         - Success.
//    1         - Failed to load module.
//
FUNCTION aziLoadModule : INT;
   fsMediaOpen(media := 1);
   IF extModuleLoad(path := "B:\SYSTEM\EXT\MOD_AZI.RMX") < 1 THEN
      aziLoadModule := 1;
   ELSE
      aziLoadModule := 0;
   END_IF;
END_FUNCTION;

#END_IF;