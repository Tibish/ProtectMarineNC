#include "ProtecMarine_Comm3G.h"

ProtecMarine_Comm3G::ProtecMarine_Comm3G(HardwareSerial& serial, const char* mqttUser, const char* mqttPassword, int pressionPin, int potPin,  int relaisA, int relaisC, int relaisE)
    : _serial(serial), _mqttUser(mqttUser), _mqttPassword(mqttPassword), _receivedMessage(""), _isJsonPayload(false), sendInterval(60000), id_ordre(),
     _pressionPin(pressionPin), _potPin(potPin), lastSendTime(0), _etat("E"), _relaisA(relaisA), _relaisC(relaisC), _relaisE(relaisE) {}

String ProtecMarine_Comm3G::getDefaultMacAddress() {
    String mac = "";
    unsigned char mac_base[6] = {0};
    if (esp_efuse_mac_get_default(mac_base) == ESP_OK) {
        char buffer[18];
        sprintf(buffer, "%02X-%02X-%02X-%02X-%02X-%02X", mac_base[0], mac_base[1], mac_base[2], mac_base[3], mac_base[4], mac_base[5]);
        mac = buffer;
    }
    return mac;
}

String ProtecMarine_Comm3G::sendData(String command, const int timeout, boolean debug) {
    String response = "";
    _serial.println(command);
    long int time = millis();
    while ((time + timeout) > millis()) {
        while (_serial.available()) {
            char c = _serial.read();
            response += c;
        }
    }
    if (debug) {
        Serial.print(response);
        delay(500);
    }
    return response;
    //checkError(command,response);
}

void ProtecMarine_Comm3G::initMQTT() {
    sendData("AT+CMQTTSTART", 1000, true);
    sendData("AT+CMQTTACCQ=0,\"PrtMarineNC\",1", 1000, true);
    sendData("AT+CMQTTWILLTOPIC=0,4", 1000, true);
    sendData("tata", 1000, true);
    sendData("AT+CMQTTWILLMSG=0,4,0", 1000, true);
    sendData("tata", 1000, true);
}

void ProtecMarine_Comm3G::connectMQTT() {
    char atCommandConnect[120];
    sprintf(atCommandConnect, "AT+CMQTTCONNECT=0,\"tcp://mqttsprotectmarine.btssn.nc:8883\",100,1,\"%s\",\"%s\"", _mqttUser, _mqttPassword);
    sendData(atCommandConnect, 1000, true);
}

void ProtecMarine_Comm3G::subscribe(String topic) {
    char fullTopic[50];
    strcpy(fullTopic, _mqttUser);
    strcat(fullTopic, "/");
    strcat(fullTopic, topic.c_str());
    int len = strlen(fullTopic);
    char atCommand[50];
    sprintf(atCommand, "AT+CMQTTSUB=0,%d,0", len);
    sendData(atCommand, 1000, true);
    sendData(fullTopic, 1000, true);
}

void ProtecMarine_Comm3G::publish(String topic, String payload) {
    char fullTopic[50];
    strcpy(fullTopic, _mqttUser);
    strcat(fullTopic, "/");
    strcat(fullTopic, topic.c_str());
    int len = strlen(fullTopic);
    char atCommandTopic[30];
    sprintf(atCommandTopic, "AT+CMQTTTOPIC=0,%d", len);
    sendData(atCommandTopic, 1000, true);
    sendData(fullTopic, 1000, true);

    const char* payloadChar = payload.c_str();
    int lenpayload = strlen(payloadChar);
    char atCommandPayload[30];
    sprintf(atCommandPayload, "AT+CMQTTPAYLOAD=0,%d", lenpayload);
    sendData(atCommandPayload, 1000, true);
    sendData(payload, 1000, true);
    sendData("AT+CMQTTPUB=0,0,60", 1000, true);
}

String ProtecMarine_Comm3G::getData() {
    StaticJsonDocument<200> doc;
    String Data;
    String id = getDefaultMacAddress();

    int mesureBrute = analogRead(_pressionPin);
    float tensionP = mesureBrute * (3.3 / 4095.0);
    //float pression = (tensionP * 2 + 4.42) * (1000.0 / (4.7 - 0.2));
    float pression = random(0, 1200);

    int potValue = analogRead(_potPin);
    //float tension = (potValue * 12.0) / 4095;
    float tension = random(0, 3.5);

    doc["mac_esp32"] = id;
    doc["id_ordre"] = id_ordre;
    doc["tension"] = tension;
    doc["pression"] = pression;
    doc["etat"] = _etat;

    serializeJson(doc, Data);
    return Data;
}

void ProtecMarine_Comm3G::handleMessage(String message) {
    message = "{" + message;
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        Serial.print(F("Erreur d'analyse du JSON: "));
        Serial.println(error.c_str());
        return;
    }

    if (doc.containsKey("action")) {
        String actions = doc["action"];
        id_ordre = doc["id_ordre"];
        if (actions == "G") {
            Serial.println("Action: Gonfler");
            setSendInterval(60000);
            Gonflage();
        } else if (actions == "D") {
            Serial.println("Action: Dégonfler");
            setSendInterval(60000);
            Degonflage();
        } else if (actions == "A") {
            Serial.println("Action: Arrêt d'urgence");
            setSendInterval(300000);
            Arret();
        } else if (actions == "T") {
            Serial.println("Action: envoie d'etat");
            publish("data",getData());
        } else {
            Serial.println("Action inconnue reçue.");
        }
    } else {
        Serial.println("Clé 'action' manquante dans le JSON.");
    }
}

void ProtecMarine_Comm3G::checkIncomingMessages() {
    while (_serial.available() > 0) {
        char c = _serial.read();
        if (c == '\n') {
            if (_isJsonPayload) {
                handleMessage(_receivedMessage);
                _isJsonPayload = false;
            }
            _receivedMessage = "";
        } else {
            _receivedMessage += c;
            if (_receivedMessage.startsWith("{")) {
                _isJsonPayload = true;
                _receivedMessage = "";
            } else if (_receivedMessage.startsWith("+CMQTTCONNLOST")) {
                sendData("AT+CMQTTDISC=0,120", 1000, true);
                connectMQTT();
                _receivedMessage = "";
            }
        }
    }
}

void ProtecMarine_Comm3G::checkError(String message, String reponse) {
    const char *searchTerm = "ERROR";

    // Utilisation de strstr pour rechercher "ERROR"
    if (strstr(reponse.c_str(), searchTerm) != NULL) {
        Serial.println("Le mot 'ERROR' a été trouvé dans la chaîne.");
        this->sendData(message, 1000, true); // Réessayez la commande
    } else {
        Serial.println("Le mot 'ERROR' n'est pas présent dans la chaîne.");
    }
}


void ProtecMarine_Comm3G::disconnectMQTT(){
    sendData("AT+CMQTTDISC=0,120", 1000, true);
    sendData("AT+CMQTTREL=0", 1000, true);
    sendData("AT+CMQTTSTOP", 1000, true);
}

void ProtecMarine_Comm3G::setSendInterval(unsigned long interval) {
    sendInterval = interval;
}

unsigned long ProtecMarine_Comm3G::getSendInterval() const {
    return sendInterval;
}

void ProtecMarine_Comm3G::verifTime(){
    if (millis() - lastSendTime >= getSendInterval()) {
      Serial.println("debut du pub");
      publish("data", getData());
      lastSendTime = millis();
    }
}

void ProtecMarine_Comm3G::Gonflage() {
    float tension = 0;
    digitalWrite(_relaisE, HIGH);
    digitalWrite(_relaisA, HIGH);
    _etat = "G";
    publish("data", getData());
    while (tension < 3.5) {
        tension = tension + 0.5;
        verifTime();
        Serial.println(tension);
        delay(500);
    }
    Serial.println("gonflage terminer");
    digitalWrite(_relaisA, LOW);
    _etat = "V";
    publish("data", getData());
    tension = 0;
    digitalWrite(_relaisC, HIGH);
    while (tension < 3.5) {
        tension = tension + 0.5;
        verifTime();
        Serial.println(tension);
        delay(500);
    }
    Serial.println("vidange terminer");
    digitalWrite(_relaisC, LOW);
    _etat = "S";
    publish("data", getData());
    id_ordre = 0;
}

void ProtecMarine_Comm3G::Degonflage() {
}

void ProtecMarine_Comm3G::Arret() {
}