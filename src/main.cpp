#include <Arduino.h>

/* necessary WiFi library */
#include <WiFi.h>

/*
  WiFi Manager to help with setting WiFi credentials at runtime
  and asynchronous connection handling
*/
#include <ESPAsyncWiFiManager.h>

/*
  library to interact with RFID card reader, includes SPI.h
*/
#include <MFRC522.h>

/*
  library to interact with the DS3231 real-time clock, includes Wire.h
*/
#include <RTClib.h>

/*
  library to work with JSON data, used to send info to the backend server
*/
#include <ArduinoJson.h>

/*
  MQTT library helps with setting up the MQTT client
  and asynchronous connection handling
*/
#include <AsyncMqttClient.h>

/*
  library to interact with the LCD Screen via I2C,
  depends on Wire.h, included
*/
#include <LiquidCrystal_I2C.h>

/*
  FreeRTOS software timers used because
  there is no built-in Ticker library
*/
extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/timers.h"
}

/* apparently there is a Ticker library */
#include <Ticker.h>

/* LED simulating alarm siren */
#define ALARM_LED 32

/* buzzer simulating alarm siren */
#define ALARM_BUZZER 33

#define WIFI_CONFIG_PIN 0

/* object instantiation for WiFi Manager setup */

AsyncWebServer server(80);
DNSServer dns;
AsyncWiFiManager wifi_manager(&server, &dns);

/* custom WiFi Manager parameters to capture MQTT broker info */

/* message instructing user to key in either MQTT broker domain name or MQTT broker IP Address*/
AsyncWiFiManagerParameter domain_or_ip("<p>Enter either the broker's IP Address or domain name(URL).</p><p>Leave the other box blank.</p>");
/* broker domain name form field */
AsyncWiFiManagerParameter mqtt_host_domain("broker-host-domain", "MQTT Domain", NULL, 30);
/* broker IP address form field */
AsyncWiFiManagerParameter mqtt_host_ip("broker-host-ip", "MQTT IP Address", NULL, 15);
/* broker username form field */
AsyncWiFiManagerParameter mqtt_user("broker-user", "MQTT Username", NULL, 20);
/* broker password form field */
AsyncWiFiManagerParameter mqtt_pass("broker-pass", "MQTT Password", NULL, 20);


/* Ticker object for flashing the onboard LED */
Ticker ticker;

/* setting default values for MQTT broker info */

/* broker's domain name/IP Address */
char broker_host[30] = "N/A";
/* broker's username */
char broker_username[20] = "default username";
/* broker's password */
char broker_password[20] = "default password";

/* IP Address object to store the MQTT broker's IP Address, if given */
IPAddress broker_ip;

/* initialising variables to handle WiFi disconnection */

/* WiFi reconnection timeout duration [ms] */
const unsigned int reconnect_timeout = 120000;
/* if device cannot reconnect to WiFi after 2 minutes, it resets */

/* epoch time at disconnect, used to measure elapsed time since disconnect */
volatile unsigned long reconnect_millis = 0;

/* device's WiFi reconnection state flag */
volatile bool reconnecting = false;

/* flag indicating whether a domain name was given for the MQTT broker instead of an IP Address */
bool domain = false;

/* MQTT instantiations */

/* asynchronous MQTT Client instance */
AsyncMqttClient mqtt_client;

/* created MQTT client's ID */
const char *mqtt_client_id = "checkpoint-A";

/* defining MQTT topics */

/* subscribe topics */

/* topic to receive the shift started/over message */
#define SHIFT_ON_OFF "sentry-platform/backend-server/shift-status"
/* topic to receive when a scan is overdue */
#define CHKS_OVERDUE "sentry-platform/circuit-handler/overdue-scan"
/* topic to receive alerts from the circuit handler */
#define RESPONSE "sentry-platform/checkpoints/A/response"
/* topic to receive alarm signal */
#define ALARM "sentry-platform/backend-server/alarm"

/* publish topics */
 
/* topic to publish scanned sentry's information */
#define SENTRY_SCAN_INFO "sentry-platform/checkpoints/sentry-scan-info"
/* topic to publish connection status */
#define CONNECTED "sentry-platform/checkpoints/connected"
/* topic to publish a scan outside the shift */
#define OUTSIDE_SHIFT_SCAN "sentry-platform/checkpoints/outside-shift-scan"

enum alerts {
  SUCCESS = 1,
  UNKNOWN_CARD = 2,
  STOLEN_CARD = 3,
  WRONG_CHECKPOINT = 4,
  WRONG_TIME = 5,
  OVERDUE_SCAN = 6,
  NO_SHIFT_SCAN = 7
};

/* checkpoint ID to send with a sentry scan */
const char *checkpoint_id = "A";

/* MQTT client reconnection timer */
TimerHandle_t mqtt_reconnection_timer;

/* MQTT publish dummy variables for dummy code */
volatile unsigned long previous_millis = 0;
const unsigned short buffer_millis = 10000;

/* 
  connected to WiFi
  avoid conflict with WiFi GotIP callback
*/
volatile bool connected = false;

/* launch WiFi config interrupt flag */
volatile bool config = false;

/* shift/circuit status flags */

/* shift ongoing/over */
volatile bool shift_status = false;

/* alarm triggered/silenced */
volatile bool alarm_on_off = false;

/* definitions for the RFID card reader */

/* connected to MFRC reset pin */
#define RST_PIN 4
/* SPI chip-select pin */
#define SS_PIN 5

/* active MFRC instance */
MFRC522 reader(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

/* string variable to store stringified RFID UID */
String card_id;

/* RTC definitions */

/* active RTC instance */
RTC_DS3231 my_RTC;

/* JSON instantiations */

/* JSON object to store the checkpoint ID, RFID UID and time of scan */
StaticJsonDocument<128> sentry_scan_info;
/* JSON object to ferry connected info */
StaticJsonDocument<128> connected_to_mqtt;

/* LCD instantiations */

/* active I2C LCD instance */
LiquidCrystal_I2C lcd(0x27, 16, 2);

/* custom check-mark symbol to display */
byte check[8] = {
  0b00000,
  0b00000,
  0b00001,
  0b00010,
  0b10100,
  0b01000,
  0b00000,
  0b00000
};

/* custom X symbol to display */
byte x_mark[8] = {
  0b00000,
  0b00000,
  0b10001,
  0b01010,
  0b00100,
  0b01010,
  0b10001,
  0b00000
};

/* prototyping functions */

/* WiFi and MQTT functions */

void toggle_alarm();
void set_broker_credentials(AsyncWiFiManagerParameter, AsyncWiFiManagerParameter, AsyncWiFiManagerParameter, AsyncWiFiManagerParameter);
void connect_to_wifi();
void config_mode_callback(AsyncWiFiManager *);
void setup_wifi_manager();
void connect_to_mqtt();
void wifi_event(WiFiEvent_t);
void on_mqtt_connect(bool);
void on_mqtt_disconnect(AsyncMqttClientDisconnectReason);
void on_mqtt_subscribe(uint16_t, uint8_t);
void on_mqtt_unsubscribe(uint16_t);
void on_mqtt_message(char *, char *, AsyncMqttClientMessageProperties, size_t, size_t, size_t);
void on_mqtt_publish(uint16_t);
void mqtt_setup_once();
void mqtt_setup_repeated();
void launch_wifi_config();

/* RFID card reader functions */

void dump_byte_array(byte *, byte);

/* LCD Screen functions */

void display_connected(uint8_t, uint8_t);
void display_default_text(uint8_t, uint8_t);
void display_connecting_to_wifi();
void display_mqtt_retry();
void display_AP_mode();
void display_scanning_verifying();
void display_valid_scan();
void display_invalid_scan(uint8_t);
void scroll_text(int, String, int, int);

volatile uint8_t alarm_reason;
volatile bool toggle = false;
volatile bool success = false;

/**
 * tick - toggles the output state of the alarm LED and alarm buzzer
 *        reads LEDs current state then changes it to the opposite
 *
 * Return: Nothing
*/
void toggle_alarm()
{
  if (toggle)
    tone(ALARM_BUZZER, 500);
  else
    noTone(ALARM_BUZZER);

  digitalWrite(ALARM_LED, digitalRead(ALARM_LED) == HIGH ? LOW : HIGH);
  toggle = !toggle;
}

/**
 * config_mode_callback - callback handler function,
 *                        called before the device enters configuration mode
 * 
 * @my_wifi_manager: active Async WiFi Manager instance (struct)
 * 
 * Return: Nothing
*/
void config_mode_callback(AsyncWiFiManager *my_wifi_manager)
{
  display_AP_mode();
  Serial.println("Entered config mode:");
  Serial.println(WiFi.softAPIP());
  Serial.println(my_wifi_manager->getConfigPortalSSID());
}

/**
 * setup_wifi - sets up WiFi Manager and Config Portal, also allows input of MQTT broker's info
 * 
 * Return: Nothing
*/
void setup_wifi_manager()
{
  /* setting up callback function that runs before the ESP goes into WiFi config mode */
  wifi_manager.setAPCallback(config_mode_callback);

  /* setting up a timeout duration for the ESP to remain in AP (config) mode before restart */
  wifi_manager.setConfigPortalTimeout(120);

  /* setting up a timeout duration for the ESP to connect to previous WiFi before going into AP mode */
  wifi_manager.setConnectTimeout(20);

  /*
    adding WiFi custom parameters to the portal
  */

  /* domain/IP guiding text */
  wifi_manager.addParameter(&domain_or_ip);
  /* add MQTT broker domain text field */
  wifi_manager.addParameter(&mqtt_host_domain);
  /* add MQTT broker IP text field */
  wifi_manager.addParameter(&mqtt_host_ip);
  /* add MQTT broker username text field */
  wifi_manager.addParameter(&mqtt_user);
  /* add MQTT broker password text field */
  wifi_manager.addParameter(&mqtt_pass);

  /* some MQTT setup code, should run just once */
  mqtt_setup_once();
}

/**
 * connect_to_wifi - connects to the WiFi, called on demand
 *                   starts an access point with name "Checkpoint A"
 *                   goes into a blocking loop awaiting configuration,
 *                   resets all WiFi Manager configs and credentials
 * 
 * Return: Nothing
*/
void connect_to_wifi()
{
  wifi_manager.resetSettings();

  domain = false;
  connected = false;

  Serial.println("Connecting to WiFi...");

  /* 'Checkpoint A' is the displayed name of the ESP access point */
  if (!wifi_manager.startConfigPortal("Checkpoint A"))
  {
    /*
      this code block is run when the config portal timeout is exhausted
    */

    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }

  /* save broker credentials */
  set_broker_credentials(mqtt_host_domain, mqtt_host_ip, mqtt_user, mqtt_pass);
}

/**
 * set_broker_credentials - saves the received broker credentials into defined variables
 * 
 * @host: MQTT broker's IP Address/domain name
 * @username: MQTT broker's username
 * @password: MQTT broker's password
 * 
 * Return: Nothing
*/
void set_broker_credentials(AsyncWiFiManagerParameter host_domain, AsyncWiFiManagerParameter host_ip, AsyncWiFiManagerParameter username, AsyncWiFiManagerParameter password)
{
  /* save broker's username */
  strncpy(broker_username, username.getValue(), username.getValueLength());
  /* save broker's password */
  strncpy(broker_password, password.getValue(), password.getValueLength());

  /*
    test for broker identity - domain name or IP address
  */

  /* returns true if a valid broker IP address is given, saves broker's IP */
  bool input_ip = broker_ip.fromString(host_ip.getValue());

  /* if either an invalid broker IP or no broker IP was given */
  if (!input_ip || (broker_ip.toString() == "0.0.0.0"))
  {
    /*
      checks if a domain name was given, no validity checks as long as it's a string
    */

    /* if no domain name was keyed in */
    if (String(host_domain.getValue()) == "")
    {
      Serial.println("Please enter a valid domain/IP, push reset button.");
      display_mqtt_retry();
      ESP.restart();
    }

    /* if a domain name (any string) was keyed in */
    else
    {
      /* save broker's domain name */
      strncpy(broker_host, host_domain.getValue(), host_domain.getValueLength());

      /* indicate that a domain name was given and not an IP Address */
      domain = true;
    }
  }

  Serial.println(broker_host);
  Serial.println(broker_username);
  Serial.println(broker_password);
  Serial.println(broker_ip);

  /* some MQTT setup code, should be run with every WiFi connection */
  mqtt_setup_repeated();

  /* connect to MQTT */
  connected = true;
  connect_to_mqtt();
}

/**
 * wifi_event - WiFi event-driven handler function
 *              executes logic depending on detected WiFi event
 * 
 * @event: WiFi event (macro)
 * 
 * Return: Nothing
*/
void wifi_event(WiFiEvent_t event)
{
  Serial.printf("[WiFi Event] event: %d\n", event);
  switch(event)
  {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("Connected to WiFi!");
      ticker.detach();

      /* reset the reconnection flag if set */
      if (reconnecting)
        reconnecting = false;

      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    /* print IP address */
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());

      if (connected)
        connect_to_mqtt();
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi connection lost. Reconnecting..");
      display_connecting_to_wifi();

      /* ensure not to attempt MQTT reconnection while WiFi disconnected */
      xTimerStop(mqtt_reconnection_timer, 0);

      /*
        set the reconnection flag if reset,
        capture epoch time at disconnect
      */
      if (!reconnecting)
      {
        reconnecting = true;
        reconnect_millis = millis();
      }

      /*
        if reconnecting,
        measure time elapsed since disconnect,
        compare with set connection timeout duration
        if exceeded, restart the device
      */
      else if (reconnecting)
      {
        if ((millis() - reconnect_millis) >= reconnect_timeout)
        {
          delay(3000);
          ESP.restart();
          delay(5000);
        }
      }

      break;
  }
}

/**
 * mqtt_setup_once - MQTT client configs that should only be set once, at device startup
 * 
 * Return: Nothing
*/
void mqtt_setup_once()
{
  /* defining the declared MQTT reconnection timer */
  mqtt_reconnection_timer = xTimerCreate("mqtt_timer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connect_to_mqtt));

  /* setting up Async MQTT client event-handling callback functions */

  /* called handler when device connects to MQTT broker */
  mqtt_client.onConnect(on_mqtt_connect);
  /* handler for when device disconnects from MQTT broker */
  mqtt_client.onDisconnect(on_mqtt_disconnect);
  /* handler for when device subscribes to an MQTT topic */
  mqtt_client.onSubscribe(on_mqtt_subscribe);
  /* handler for when device unsubscribes from an MQTT topic */
  mqtt_client.onUnsubscribe(on_mqtt_unsubscribe);
  /* handler for when device receives a message published on any subscribed MQTT topic */
  mqtt_client.onMessage(on_mqtt_message);
  /* handler for when device publishes a message to an MQTT topic */
  mqtt_client.onPublish(on_mqtt_publish);

  /* setting a client ID, needed for final message retention */
  mqtt_client.setClientId(mqtt_client_id);
  /* setting up client keep-alive (heartbeat packet) timer */
  mqtt_client.setKeepAlive(60);

  /* setting up LWT for the client in case of unprecedented disconnection */
  connected_to_mqtt["id"] = mqtt_client_id; /* checkpoint */
  connected_to_mqtt["connected"] = 0; /* connected to MQTT */

  /* serialising JSON object to JSON string */
  String connection_info;
  serializeJson(connected_to_mqtt, connection_info);

  mqtt_client.setWill(CONNECTED, 2, false, connection_info.c_str());
}

/**
 * mqtt_setup_repeated - MQTT client setup code that should be run on every WiFi (re)connection
 * 
 * Return: Nothing
*/
void mqtt_setup_repeated()
{
  /* setting the MQTT client's credentials to connect to the server */

  /* configuring the broker credentials into the client object to connect */
  mqtt_client.setCredentials(broker_username, broker_password);

  /*
    set up with either domain name or IP Address,
    depending on which was given
  */
  if (domain)
  {
    Serial.println("using domain name");
    mqtt_client.setServer(broker_host, 1883);
  }
  else if (!domain)
  {
    Serial.println("using IP address");
    mqtt_client.setServer(broker_ip, 1883);
  }
}

/**
 * connect_to_mqtt - connects the ESP MQTT client to the MQTT broker over WiFi
 * 
 * Return: Nothing
*/
void connect_to_mqtt()
{
  // display_connecting_to_mqtt();
  Serial.println("Connecting to MQTT broker...");
  mqtt_client.connect();
}

/**
 * on_mqtt_connect - event handler for post MQTT connection actions
 * 
 * @session_present: id of the present session
 * 
 * Return: nothing
*/
void on_mqtt_connect(bool session_present)
{
  Serial.println("Connected to MQTT!");
  Serial.print("Session present: ");
  Serial.println(session_present);

  /* publish to the web app that the device is MQTT (and WiFi) connected */

  connected_to_mqtt["id"] = mqtt_client_id; /* checkpoint */

  connected_to_mqtt["connected"] = true; /* connected to MQTT */

  /* serialising JSON object to JSON string */

  String connection_info;
  serializeJson(connected_to_mqtt, connection_info);
  Serial.println(connection_info.c_str());

  delayMicroseconds(3000000);

  mqtt_client.publish(CONNECTED, 2, false, connection_info.c_str());

  /* subscribe to the relevant topics */

  mqtt_client.subscribe(SHIFT_ON_OFF, 2);
  mqtt_client.subscribe(RESPONSE, 2);
  mqtt_client.subscribe(ALARM, 2);
  mqtt_client.subscribe(CHKS_OVERDUE, 2);
}

/**
 * on_mqtt_disconnect - event handler for post MQTT disconnection actions
 * 
 * @reason: reason code for MQTT disconnection
 * 
 * Return: nothing
*/
void on_mqtt_disconnect(AsyncMqttClientDisconnectReason reason)
{
  Serial.println("Disconnected from MQTT.");
  Serial.printf("Reason: %d\n", reason);

  if (WiFi.isConnected())
    xTimerStart(mqtt_reconnection_timer, 0);
}

/**
 * on_mqtt_subscribe - event handler for post MQTT subscription actions
 * 
 * @packet_id: ID of subscription return packet
 * @qos: subscription's quality-of-service level
 * 
 * Return: nothing
*/
void on_mqtt_subscribe(uint16_t packet_id, uint8_t qos)
{
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packet ID: ");
  Serial.println(packet_id);
  Serial.print("  qos: ");
  Serial.println(qos);
}

/**
 * on_mqtt_unsubscribe - event handler for post MQTT unsubscription actions
 * 
 * @packet_id: ID of unsubscription return packet
 * 
 * Return: nothing
*/
void on_mqtt_unsubscribe(uint16_t packet_id)
{
  Serial.print("Unsubscribe acknowledged.");
  Serial.print(" packet ID: ");
  Serial.println(packet_id);
}

/**
 * on_mqtt_message - event handler for post MQTT message reception actions
 *                   main controller for directing follow-up actions for messages received from subscriptions
 * 
 * @topic: MQTT topic on which message was posted
 * @payload: message contents
 * @properties: MQTT message properties (retain flag, QoS, etc)
 * @len: length of the MQTT payload string
 * @index: index of the incoming MQTT message
 * @total: total length of topic + payload
 * 
 * Return: Nothing
*/
void on_mqtt_message(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{ 
  Serial.println("Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);

  /* dumping the received bytearray payload into a string */
  String message;
  for (unsigned int i = 0; i < len; i++)
    message += (char)payload[i];

  Serial.println(message);

  /* checking the topic on which the incoming message was published */
  if (!strcmp(topic, SHIFT_ON_OFF))
  {
    if (message == "ON")
      shift_status = true;
    else
    {
      /* also used to notify the checkpoints that the monitoring platform has disconnected from the broker */
      /* reason could be that the server has gone down */

      shift_status = false; /* set shift to 'over' */
      alarm_on_off = false; /* reset the alarm flag */
      ticker.detach(); /* deactivate the alarm */
    }
  }

  else if (!strcmp(topic, ALARM))
  {
    if (message == "ON")
    {
      Serial.println("alarm triggered");
      alarm_on_off = true;
      ticker.attach(0.5, toggle_alarm);
    }
    else
    {
      alarm_on_off = false;
      alarm_reason = 0;
      Serial.println("alarm silenced");
      ticker.detach();
      digitalWrite(ALARM_LED, LOW);
      noTone(ALARM_BUZZER);
    }
  }

  else if (!strcmp(topic, CHKS_OVERDUE))
  {
    alarm_reason = 6;
  }

  else if (!strcmp(topic, RESPONSE))
  {
    uint8_t code = atoi(message.c_str());

    if (code == 1)
    /* set flag to display success message on the LCD screen */
      success = true;
    else
      alarm_reason = code;
  }
}

/**
 * on_mqtt_publish - event handler for post MQTT publish actions
 * 
 * @packet_id: ID of publish return packet
 * 
 * Return: nothing
*/
void on_mqtt_publish(uint16_t packet_id)
{
  Serial.print("Publish acknowledged.");
  Serial.print(" packet ID: ");
  Serial.println(packet_id);
}

/**
 * launch_wifi_config - sets flag that indicates that device should go into
 *                      on-demand WiFi config mode, triggered by ISR
 * 
 * Return: Nothing
*/
void IRAM_ATTR launch_wifi_config()
{
  config = true;
}

/**
 * dump_byte_array - dumps the scanned hex RFID UID into a string literal
 * 
 * @buffer: buffer storing scanned RFID UID
 * @buffer_size: length of the RFID UID in bytes (4, 7 or 10)
 * 
 * Return: string literal representation of the scanned RFID UID
*/
void dump_byte_array(byte *buffer, byte buffer_size)
{
  for (byte i = 0; i < buffer_size; i++)
  {
    if (i == 0)
      card_id += (buffer[i] < 0x10 ? "0" : "");
    else
      card_id += (buffer[i] < 0x10 ? " 0" : " ");

    card_id += String(buffer[i], HEX);
  }
}

/**
 * display_connected - displays WiFi and MQTT check marks when both are connected
 *                     on display's top row
 * 
 * Return: Nothing
*/
void display_connected(uint8_t symbol_wifi, uint8_t symbol_mqtt)
{
  lcd.setCursor(0, 0);
  lcd.print("WiFi: ");
  lcd.setCursor(6, 0);
  lcd.write(symbol_wifi);
  lcd.setCursor(7, 0);
  lcd.print(" MQTT: ");
  lcd.setCursor(14, 0);
  lcd.write(symbol_mqtt);
  lcd.setCursor(15, 0);
  lcd.print(" ");
}

/**
 * display_default_text - displays "Scan Card" on the bottom row of the display
 * 
 * Return: Nothing
*/
void display_default_text(uint8_t wifi_symbol, uint8_t mqtt_symbol)
{
  display_connected(wifi_symbol, mqtt_symbol);

  lcd.setCursor(0, 1);
  lcd.print("   Scan Card    ");
}

/**
 * display_connected_to_wifi - displays "Connecting to WiFi" on the display
 *                             called in WiFi disconnect event handler
 * 
 * Return: Nothing
*/
void display_connecting_to_wifi()
{
  lcd.setCursor(0, 0);
  lcd.print(" Connecting to  ");
  lcd.setCursor(0, 1);
  lcd.print("    WiFi....    ");
}

/**
 * display_connecting_to_mqtt - displays "Connecting to MQTT" on the display
 *                             called in connect_to_mqtt
 * 
 * Return: Nothing
*/
void display_mqtt_retry()
{
  lcd.setCursor(0, 0);
  lcd.print("Enter valid MQTT");
  lcd.setCursor(0, 1);
  lcd.print(" Domain/IP Addr ");
  delay(3000);
  lcd.setCursor(0, 0);
  lcd.print("   Restarting   ");
  lcd.setCursor(0, 1);
  lcd.print("     Device     ");
  delay(3000);
}

/**
 * display_AP_mode - displays message informing that the device is in AP/WiFi config mode
 *                   called in config_mode_callback
 * 
 * Return: Nothing
*/
void display_AP_mode()
{
  lcd.setCursor(0, 0);
  lcd.print(" ! A.P. Mode !  ");
  lcd.setCursor(0, 1);
  lcd.print("Set WiFi & MQTT ");
}

/**
 * display_scanning_verifying - displays "Scanning and verifying sentry ID" when the card is scanned
 *                              and sent to the sentry platform, and awaiting a verification response
 * 
 * Return: Nothing
*/
void display_scanning_verifying()
{
  display_connected(1, 1);

  String valid = "Scanning and verifying sentry ID..";
  scroll_text(1, valid, 375, 16);
}

/**
 * display_valid_scan - displays a valid scan message on the display,
 *                      on response from the sentry monitoring platform
 * 
 * Return: Nothing
*/
void display_valid_scan()
{
  String valid = "Valid scan! Continue to next checkpoint..";
  scroll_text(1, valid, 375, 16);
}

/**
 * display_valid_scan - displays an invalid scan message on the display,
 *                      on response from the sentry monitoring platform
 *                      displays with the reason: unknown ID, wrong sentry, wrong time
 * 
 * @reason: flag corresponding to reason for invalid scan
 *          1: unknown ID
 *          2: wrong/unexpected sentry
 *          3: wrong time of scan
 *
 * Return: Nothing
*/
void display_invalid_scan(uint8_t reason)
{
  lcd.setCursor(0, 0);
  lcd.print(" INVALID SCAN!  ");

  String unexpected = "WRONG CHECKPOINT!";
  String wrong_time = "WRONG TIME OF SCAN!";

  switch(reason)
  {
    case UNKNOWN_CARD:
      lcd.setCursor(0, 1);
      lcd.print("  UNKNOWN ID!   ");
      break;
    case STOLEN_CARD:
      lcd.setCursor(0, 1);
      lcd.print("  STOLEN CARD!  ");
      break;
    case WRONG_CHECKPOINT:
      scroll_text(1, unexpected, 250, 16);
      break;
    case WRONG_TIME:
      scroll_text(1, wrong_time, 250, 16);
      break;
    case NO_SHIFT_SCAN:
      lcd.setCursor(0, 1);
      lcd.print("NO ONGOING SHIFT");
  }
}

/**
 * display_scan_time_elapsed - displays message indicating that an expected sentry
 *                             did not scan within their given check-in window
 * 
 * Return: Nothing
*/
void display_scan_time_elapsed()
{
  lcd.setCursor(0, 0);
  lcd.print("SENTRY VERIFYING");
  lcd.setCursor(0, 1);
  lcd.print(" WINDOW PASSED! ");
}

/**
 * scroll_text - displays a scrolling message on the display,
 *               if the message's length exceeds 16 characters
 * 
 * @row: display row on which to display message: 0 or 1
 * @message: message to display
 * @delay_time: time delay before scrolling to the next letter
 * @lcd_columns: number of display's columns = 16
 * 
 * Return: Nothing
*/
void scroll_text(int row, String message, int delay_time, int lcd_columns)
{
  for (int i = 0; i < 3; i++)
  {
    message = " " + message;
  }
  message += " ";

  for (int pos = 0; pos < message.length() - 13; pos++)
  {
    lcd.setCursor(0, row);
    lcd.print(message.substring(pos, pos + lcd_columns));
    delay(delay_time);
  }
}

void setup() {
  /* setting up the ESP32 in station mode (WiFi client) */
  WiFi.mode(WIFI_STA);

  Serial.begin(115200);

  /* initialise SPI, I2C, MFRC, RTC and LCD comms */

  SPI.begin();
  Wire.begin();
  reader.PCD_Init();
  my_RTC.begin();
  lcd.init();
  lcd.backlight();

  /* saving the custom checkmark to the LCD's memory */
  lcd.createChar(1, check);
  lcd.createChar(2, x_mark);

  /*
    set time of the RTC
    adjusts it to time at which the code was compiled
    time reference is time on the device on which the compiler ran
    NB: set timezone of device to GMT+0 to set correct UTC epoch time
        i.e. without timezone offset
  */
  if (my_RTC.lostPower())
    my_RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));

  /* setting up pin connected to alarm LED as an output to flash */
  pinMode(ALARM_LED, OUTPUT);

  /* setting up pin connected to buzzer */
  pinMode(ALARM_BUZZER, OUTPUT);

  /* setting up input pin to listen for on-demand trigger (button) */
  pinMode(WIFI_CONFIG_PIN, INPUT_PULLUP);

  /* configure callback function to handle WiFi events */
  WiFi.onEvent(wifi_event);

  /* set up WiFi Manager configs, callbacks, parameters */
  setup_wifi_manager();

  /* set up a hardware interrupt to trigger on-demand WiFi config portal */
  attachInterrupt(digitalPinToInterrupt(WIFI_CONFIG_PIN), launch_wifi_config, FALLING);
}

void loop()
{
  /* if WiFi config mode button pressed */
  if (config)
  {
    connect_to_wifi();

    /* reset interrupt flag */
    config = false;
  }
  
  if (alarm_on_off)
  {
    if (alarm_reason == OVERDUE_SCAN)
      display_scan_time_elapsed();
    else
      display_invalid_scan(alarm_reason);
    return;
  }

  if (success)
  {
    display_valid_scan();
    success = false;
  }

  /* 
    conditions with return statements:
    running code below them is pointless hence early return, restart loop
  */

  /* if both WiFi and MQTT connected, display check on both */
  if (WiFi.isConnected() && mqtt_client.connected())
    display_default_text(1, 1);

  /* if only WiFi connected: display check on WiFi, X on MQTT */
  else if (WiFi.isConnected())
  {
    display_default_text(1, 2); return;
  }

  /* if neither connected: display X on both */
  else
  {
    display_default_text(2, 2); return;
  }

  /* checking if there is a 'new' RFID card in vicinity to scan */
  if (!reader.PICC_IsNewCardPresent())
    return;

  if (!reader.PICC_ReadCardSerial())
    return;

  /* dumping the scanned card's ID (hex number) into a string */
  dump_byte_array(reader.uid.uidByte, reader.uid.size);
  /* extracting the current epoch time */
  DateTime now = my_RTC.now();

  display_scanning_verifying();

  /* saving the checkpoint's ID, scanned RFID UID and time of scan (epoch) into a JSON object */

  sentry_scan_info["checkpoint-id"] = checkpoint_id; /* checkpoint */
  sentry_scan_info["sentry-id"] = card_id; /* RFID UID */
  sentry_scan_info["scan-time"] = now.unixtime() + 46; /* epoch time of scan */

  /* serialising JSON object to JSON string */
  String sent_sentry_info;
  serializeJson(sentry_scan_info, sent_sentry_info);

  /* 
    publish - (required) convert JSON string to byte(char) array before publish
    String.c_str() = const char*
  */
 
  /* if scan not during shift - PROBLEM */
  if (!shift_status)
  {
    mqtt_client.publish(OUTSIDE_SHIFT_SCAN, 2, false, sent_sentry_info.c_str());
    alarm_reason = NO_SHIFT_SCAN;
  }
  else
    mqtt_client.publish(SENTRY_SCAN_INFO, 2, false, sent_sentry_info.c_str());

  /* reset stored RFID UID to an empty string for next scan */
  card_id = "";
}
