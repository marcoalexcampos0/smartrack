/* TESTAR:
 * 
 * PORTAS 1 e 2 OK
 * Numero máximo de sensores DS18B20 (mudar para 1 e ver se funciona) OK
 * Verificar se os traps da temperatura 0 e 1 continuam a sair correctamente OK
 * Colocar as etiquetas nos cabos ~OK
 * Leitura das PORTAS (calibrar amperagem)
 * Validar output na consola, ZABBIX e WEB+JSON
 * 
 * a) Estado das portas (criada função verifica_portas() ) OK
 * b) Função pausa OK
 * c) Output do JSON (webserver) OK
 * d) URL "/TEMP1" OK
 * e) Verificação dos dados enviados pelo sendzabbix (zabbix trapper) porque mudamos o nome para "hostname" (snifar o pacote enviado e/ou activar o debug dentro da função) OK
 * f) Testar o ligar/desligar via web (ainda por fazer) das tomadas electricas ("remote-reboot")
 * g) Todo o resto que foi modificado.... :)
 * 
 * ---------------------------------------------------------------
 * Este código:
 * - Lê sondas  - DS18B20 (temperatura)
 *              - DHT22 (temperatura e humidade)
 *              - Contactos secos (sensor de porta)
 *              - SCT-013 (corrente)
 *              
 * - Disponibiliza um WEB Server - Leitura individual de cada sensor (/TEMP1, TEMP2, etc)
 *                               - Leitura global (URL /) em - JSON
 *                                                           - XML
 *                                                           - HTML
 * 
 * - Envia Traps para um servidor/proxy ZABBIX
 * 
 * - Lê o UPTIME
 * 
 * - Mantêm um registo e informa o status dos alarmes via um LED de Status do RACK
 * 
 * - Disponibiliza um LOG na consola com 4 níveis de DEBUG
 * ---------------------------------------------------------------
 */


/*
 * smartRACK | SMARTrack | SmartRack | SMARTRack
 * Copyright 2018 ® Claranet / ITEN
 * Marco Campos <marco.campos@pt.clara.net>
 * 
 * Usa bibliotecas de terceiros com os respectivos copyright.
 * 
 * Falta:
 *    - Incluir as bibliotecas de SNMP (não é possível no UNO/Ethernet, só no MEGA) (ethernet feito)
 *    - Actualizar o LED_PORTA de STATUS do bastidor (verde, amarelo, vermelho, sitilar/empiscar) (tudo menos piscar)
 *    - Processar os pedidos SNMP read e gerar TRAPS (feito para Zabbix_sender)
 *    - Criar rotinas para processar INTerrups gerados pela mudança de estado dos sensores das portas (pin 3)
 *    - Processar os pedidos SNMP write (p.ex. ligar a luz/alarme)
 *    - Incluir outros sensores como Amperagem (AMPE1 e AMPE2)
 *    - mini-CLI
 *    - Gravar settings na EEPROM (ip, netmask, gw, dns, DHCP, contagens kW, hostname, etc.)
 *    - Interface WEB para configuração?
 *    - Real-time clock & watchdog
 *    - etc
*/

  #include <SPI.h>
  #include <Ethernet.h>
  #include <Base64.h>
  #include <Adafruit_Sensor.h>
  #include <DHT.h>
  #include <DHT_U.h>
  #include <OneWire.h>
  #include <DallasTemperature.h>

// Variáveis específicas para cada sonda *** EDITAR ANTES DE COMPILAR ***:
  byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x03 };
  IPAddress ip(192,168,193,123);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress gateway(192, 168, 193, 1);
  IPAddress zabbix(195, 22, 17, 155); 
  const char hostname[] = "SMARTRACK-F0R0-ERM"; // Obrigatóriamente com 18 caracteres!!!
  int debug = 2; // 0: ALARM, 1: ERRO, 2: INFO, 3: DEBUG (não usado ainda)

//----------------------------------------------------------------------------------------------------------------------------------

//* Purpose : Zabbix Sender * 
  //* Author : Schotte Vincent * https://forum.arduino.cc/index.php?topic=189611.0
  //  #include <Base64.h>
  //  #include <SPI.h>
  //  #include <Ethernet.h>
  //  byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x03 };
  //  IPAddress ip(192,168,193,123);
  //  IPAddress gateway(255, 255, 255, 0);
  //  IPAddress subnet(192, 168, 193, 1);
  //  IPAddress zabbix(195, 22, 17, 155); 
  EthernetClient zabclient ;
  // Initialize the Ethernet server library
  // with the IP address and port you want to use 
  // (port 80 is default for HTTP):
  EthernetServer server(80);


// Usadas pelo sensor DHT22
  //  #include <Adafruit_Sensor.h>
  //  #include <DHT.h>
  //  #include <DHT_U.h>
  // Uncomment the type of sensor in use:
  //#define DHTTYPE           DHT11     // DHT 11 
  //#define DHTTYPE           DHT21     // DHT 21 (AM2301)
  // See guide for details on sensor wiring and usage:
  //   https://learn.adafruit.com/dht/overview
  #define DHTTYPE           DHT22     // DHT 22 (AM2302)
  #define DHTPIN            2         // Pin which is connected to the DHT sensor.
  DHT_Unified dht(DHTPIN, DHTTYPE);

// Usadas pelo sensor DS18B20
  //  #include <OneWire.h>
  //  #include <DallasTemperature.h>
  // Armazena temperaturas minima e maxima
  // float tempMin = 999;
  // float tempMax = 0;
  // Define uma instancia do oneWire para comunicacao com o sensor
  #define ONE_WIRE_BUS 7
  OneWire oneWire(ONE_WIRE_BUS);
  DallasTemperature sensors(&oneWire);
  const short int numeroMaxSensoresDS18B20 = 2;
  DeviceAddress sensorsDS18B20[numeroMaxSensoresDS18B20];
  int numeroSensoresDS18B20;

// Variaveis globais
  uint32_t delayMS;
  // int ANTERIOR = 0;
  // int DIFERENCA = 0;
  #define LED_STATUS_PIN 5
  float DHT22_humi = 0;
  float DHT22_temp = 0;
  float LM35DZ_temp = 0;
  float DS18B20_temp[numeroMaxSensoresDS18B20] = {};
  int led_status = 0;
  //const String INFO = ": INFO: ";
  //const String ERRO = ": ERRO: ";

// Pinças amperimetricas
  // http://www.naylampmechatronics.com/blog/51_tutorial-sensor-de-corriente-ac-no-invasivo-s.html
  #define AMPE1_PIN A4
  #define AMPE2_PIN A5
  float correnteAMPE1;
  float correnteAMPE2;

// Relés das tomadas de energia ("remote-reboot")
  //  int powerPin[] = {9, 0, 0, 0};     // Define os pinos para cada relé
  //  boolean rele[] = {false, false, false, false};  // Array com o estado de cada relé

// Portas
  int DOOR[] = { 0, 1 };
  int PORTA_PIN[] = { 8, 9 };
  boolean ESTADO_PORTA[] = { false, false };
  boolean ESTADO_PORTA_ANTERIOR[] = { false, false };
  String PORTA_status[] = { "Fechada", "Fechada" } ;

// Estados de Status do LED
  #define GREEN 0
  #define YELLOW 1
  #define RED 2
  #define MAJOR_ALARM 3 // Blink RED

// Sistema de Alarmes
  // Valores no array "alarmes" dos sensores
  //#define DOOR1 0
  //#define DOOR2 1
  #define TEMP1 2
  #define TEMP2 3
  #define AMPE1 4
  #define AMPE2 5

  // Pinos do LED RGB
  #define status_led1 6
  #define status_led2 3
  #define status_led3 0

  // Array dos alarmes (DOOR1, DOOR2, TEMP1, TEMP2, AMPE1, AMPE2, etc..)
  int alarms[] = {0, 0, 0, 0, 0, 0};

unsigned long last_millis = 4294967290;
unsigned long network_time = 0;

void setup() {

  // Incia LED de status do equipamento
  pinMode(LED_STATUS_PIN,OUTPUT);

  // Inicia porta série
  digitalWrite(LED_STATUS_PIN,LOW);
  Serial.begin(9600);
  digitalWrite(LED_STATUS_PIN,HIGH);

  // Inicia pinças amperimétricas
  analogReference(INTERNAL);
  pinMode(AMPE1_PIN, INPUT);
  pinMode(AMPE2_PIN, INPUT);

  // Inicia inos do LED RGB (status do rack):
  pinMode(status_led1,OUTPUT);
  pinMode(status_led2,OUTPUT);
  pinMode(status_led3,OUTPUT);

  digitalWrite(LED_STATUS_PIN,HIGH);

  Serial.println();
  Serial.println(F("smartRACK (0.9.0)"));
  Serial.println(F("Copyright 2018 ® Claranet/ITEN "));
  Serial.println(F("by Marco Campos <marco.campos@pt.clara.net> "));
  Serial.println();
  Serial.println(F("<protótipo / proof of concept>"));
  Serial.println();

  // Inicia a placa de rede (com DHCP ou IP fixo)
  //Serial.print(F("A inicializar a Ethernet (DHCP)... "));
  // start the Ethernet connection:
  //if (Ethernet.begin(mac) == 0) {
  //  Serial.println(F("falhou, a usar o IP pré-configurado\n\n"));
    Ethernet.begin(mac, ip);
  //}

  // Inicia o servidor WEB
  server.begin();

  // Inicia o sensor DHT22
  digitalWrite(LED_STATUS_PIN,LOW);
  dht.begin();
  digitalWrite(LED_STATUS_PIN,HIGH);

  Serial.println(F("Sensores:\n"));

  sensor_t sensor;

  // Print temperature sensor details.
  dht.temperature().getSensor(&sensor);
  Serial.println(F("--[DHT:Temperature]--"));
  Serial.print  (F("Sensor:     ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver: ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:  ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:  ")); Serial.print(sensor.max_value); Serial.println(F(" *C"));
  Serial.print  (F("Min Value:  ")); Serial.print(sensor.min_value); Serial.println(F(" *C"));
  Serial.print  (F("Resolution: ")); Serial.print(sensor.resolution); Serial.println(F(" *C"));  

  // Print humidity sensor details.
  dht.humidity().getSensor(&sensor);
  Serial.println();
  Serial.println(F("--[DHT:Humidity]--"));
  Serial.print  (F("Sensor:     ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver: ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:  ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:  ")); Serial.print(sensor.max_value); Serial.println(F("%"));
  Serial.print  (F("Min Value:  ")); Serial.print(sensor.min_value); Serial.println(F("%"));
  Serial.print  (F("Resolution: ")); Serial.print(sensor.resolution); Serial.println(F("%"));  
  // Set delay between sensor readings based on sensor details.
  delayMS = sensor.min_delay / 1000;

  // Inicializa sensores DS18B20
  digitalWrite(LED_STATUS_PIN,LOW);
  Serial.println();
  Serial.println(F("--[DS18B20]--"));
  sensors.begin();
  // Localiza e mostra enderecos dos sensores
  //Serial.println(F("A localizar sensores DS18B20..."));
  Serial.print(F("Foram encontrados "));
  numeroSensoresDS18B20 = sensors.getDeviceCount();
  Serial.print(numeroSensoresDS18B20, DEC);
  Serial.print(F(" sensores."));
  if ( numeroMaxSensoresDS18B20 < numeroSensoresDS18B20) {
    Serial.print(F("(maximo ")); Serial.print(numeroMaxSensoresDS18B20); Serial.print(F(")"));
    numeroSensoresDS18B20 = numeroMaxSensoresDS18B20;
  }
  Serial.println();
  if (!sensors.getAddress(sensorsDS18B20[0], 0)) {
     Serial.println(F("Sensores nao encontrados !")); 
  } else {
    // Mostra o endereco do sensor encontrado no barramento
    Serial.print(F("Endereco sensor: "));
    for (int i =0; i<numeroSensoresDS18B20; i++) {
      sensors.getAddress(sensorsDS18B20[i], i);
      mostra_endereco_sensor(sensorsDS18B20[i]);
      if (i<(numeroSensoresDS18B20-1)) {
        Serial.print(", ");
      }
    }
  }

  Serial.println();
  digitalWrite(LED_STATUS_PIN,HIGH);

  // Inicializa os sensores das portas
  pinMode(PORTA_PIN[0],INPUT);
  pinMode(PORTA_PIN[1],INPUT);
  
  //Serial.print(F("IP Address: ")); Serial.print(printIPAddress(Ethernet.localIP()));
  Serial.println();
  Serial.print(F("IP Address: ")); Serial.println( printIPAddress(Ethernet.localIP()) + " / " + printIPAddress(subnet) + " (" + printIPAddress(ip) + ")");
  Serial.print(F("   Gateway: ")); Serial.println(printIPAddress(gateway));
  Serial.print(F("Zabbix Srv: ")); Serial.println(printIPAddress(zabbix));

  Serial.println();
  Serial.println(F("Inicialização completa.\n"));

  Serial.println();
  set_status_led(0, 0);

  pausa(1, false); 
}

void lerPincasAmp()
{
  correnteAMPE1 = get_corriente(AMPE1_PIN);
  correnteAMPE2 = get_corriente(AMPE2_PIN);
  if ( debug >= 2) { Serial.print(get_time()); Serial.print(F(": INFO: Corrente #1: ")); Serial.print( correnteAMPE1 ); Serial.print(F("A, ")); Serial.print(F("#2: ")); Serial.print( correnteAMPE2 ); Serial.println(F("A")); }

  // Converte um float em array de 5 caracteres:
  char AMPE_temp_char[5];
  String(correnteAMPE1,2).toCharArray(AMPE_temp_char, 6);
  sendzabbix("AMPE1", AMPE_temp_char);

  String(correnteAMPE2,2).toCharArray(AMPE_temp_char, 6);
  sendzabbix("AMPE2", AMPE_temp_char);
}

float get_corriente(int AMPE_PIN)
{
  //Faz amostragens durante 0,5s e faz a média (rms)
  float voltajeSensor;
  float corriente=0;
  float Sumatoria=0;
  unsigned long tiempo=millis();
  int N=0;
  while(millis()-tiempo<500)//Duración 0.5 segundos(Aprox. 30 ciclos de 60Hz)
  { 
    //voltajeSensor = analogRead(AMPE_PIN) * (1.1 / 1023.0);////voltaje del sensor
    voltajeSensor = analogRead(AMPE_PIN) * (1.0 / 1023.0);////voltaje del sensor
    //corriente=voltajeSensor*30.0; //corriente=VoltajeSensor*(30A/1V)
    corriente=voltajeSensor*6.0; //corriente=VoltajeSensor*(30A/1V)
    Sumatoria=Sumatoria+sq(corriente);//Sumatoria de Cuadrados
    N=N+1;
    delay(1);
  }
  Sumatoria=Sumatoria*2;//Para compensar los cuadrados de los semiciclos negativos.
  corriente=sqrt((Sumatoria)/N); //ecuación del RMS
  return(corriente);
}

String printIPAddress(IPAddress IP)
{
  String IPstring = "";
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    IPstring += String(IP[thisByte], DEC);
    if ( thisByte < 3 ) { IPstring += "."; }
  }
  return IPstring;
}

unsigned long get_network_time()
{
  // Esta função era suposto ir buscar o tempo certo via NTP ou outro protocolo... mas por restrições de memória não vamos fazer para já...
  //unsigned long t = 443405; // 5 dias, 3 horas, 10 minutos e 5 segundos (usado como exemplo);
  unsigned long t = 0; 
  if (debug >= 2 )
  {
    Serial.print(F("0:0:0:0: INFO: Geting network time... "));
    Serial.print(t); Serial.println(F("s"));    
  }
  return t;
}

String get_time()
{
  unsigned long t = millis();

  // Quando inicializa (millis ~= 0), ou quando o contador "dá a volta" (50 dias), vai buscar a hora certa:
  if ( t <= last_millis ) {
    //network_time = get_network_time();
    last_millis = t;
  } else {
    last_millis = t;
  }

  unsigned long seg = ((network_time * 1000) + t) / 1000;

  // Calcula os dias, horas, minutos e segundos com base nos millisegundos do uptime:
  unsigned int dias = seg / 86400; unsigned long resto_dos_dias = seg % 86400;
  byte horas = resto_dos_dias / 3600; unsigned long resto_das_horas = resto_dos_dias % 3600;
  byte minutos = resto_das_horas / 60; unsigned long resto_dos_minutos = resto_das_horas % 60;
  byte segundos = resto_dos_minutos;

//  String time = String(dias) + ":" + String(horas) + ":" + String(minutos) + ":" + String(segundos);
//  return time;
  return String(dias) + ":" + String(horas) + ":" + String(minutos) + ":" + String(segundos);
}

void set_status_led (int sensor, int alarm) {
  led_status = 0;
  alarms[sensor] = alarm;

  for (int i=0; i<6; i++) {
    if (alarms[i] > led_status) {
      led_status = alarms[i];
    }
  }

  switch(led_status) {
    case 0:
      if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: Coloca o LED de status a VERDE")); Serial.print(F(". Sensor: ")); Serial.println(sensor); }
      digitalWrite(status_led1, 0); // RED
      digitalWrite(status_led2, 1); // GREEN
      digitalWrite(status_led3, 0); // BLUE
      break;
    case 1:
      if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: Coloca o LED de status a AMARELO")); Serial.print(F(". Sensor: ")); Serial.println(sensor); }
      digitalWrite(status_led1, 1);
      digitalWrite(status_led2, 1);
      digitalWrite(status_led3, 0);
      break;   
    case 2:
      if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: Coloca o LED de status a VERMELHO")); Serial.print(F(". Sensor: ")); Serial.println(sensor); }
      digitalWrite(status_led1, 1);
      digitalWrite(status_led2, 0);
      digitalWrite(status_led3, 0);
      break;
    case 3:
// Criar vermelho a piscar
      break;
  }
}

void mostra_endereco_sensor(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // Adiciona zeros se necessário
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

void lerDS18B20() {
// Estado do sensor DS18B20 (temperatura digital):
  sensors.requestTemperatures();

  for (int i=0; i<numeroSensoresDS18B20; i++) {
    DS18B20_temp[i] = sensors.getTempC(sensorsDS18B20[i]);
    if ( i == 1 ) { DS18B20_temp[i] = DS18B20_temp[i] + 0.30; } // Se o sensor for o "28FF5CE00217039A" então acrescenta 0,3º (erro do sensor 'chinoca')

  
//    if ( DS18B20_temp[i] == -127 ) {
//      if ( debug >= 1) { Serial.print(get_time()); Serial.print(F(": ERRO: Erro ao ler o sensor DS18B20: ")); mostra_endereco_sensor(sensorsDS18B20[i]); Serial.println(); }
//    } else {
      // Mostra dados na consola
      if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: Temperatura (DS18B20): ")); Serial.print(DS18B20_temp[i]); Serial.print(F("ºC : ")); mostra_endereco_sensor(sensorsDS18B20[i]); Serial.println(); }

      // Converte um float em array de 5 caracteres e o nome da sonda (TEMP0 ou TEMP1)
      char DS18B20_ID_char[5];
      char DS18B20_temp_char[5];
      String a = "TEMP" + String(i+1);
      a.toCharArray(DS18B20_ID_char, 6);
      String(DS18B20_temp[i],2).toCharArray(DS18B20_temp_char, 6);
      // Serial.println();Serial.print(DS18B20_ID_char); // Serial.print(":"); Serial.println(DS18B20_temp_char);
      sendzabbix(DS18B20_ID_char, DS18B20_temp_char);

      if (DS18B20_temp[i] < 27 && DS18B20_temp[i] > 20) {
        if ( alarms[i+2] != GREEN ) { set_status_led(i+2, GREEN); }
        }
        else {
          if ( alarms[i+2] != YELLOW ) { set_status_led(i+2, YELLOW); }
        }    
    //}
  }
}

void lerLM35DZ() {
// Estado do sensor LM35DZ (temperatura analógico):
  int VALOR = analogRead(A0);
  if ( VALOR > 300 ) {
    if ( debug >= 1) { Serial.print(get_time()); Serial.println(F(": ERRO: Erro ao ler o sensor LM35DZ")); }
  } else {
    LM35DZ_temp = 5 * VALOR / 9.55;
    //  Serial.print("  Temperatura: "); Serial.print( 5 * VALOR / 10.23 ); Serial.print("º ("); Serial.print(VALOR); Serial.println(F(")"));
    if ( debug >= 2) { Serial.print(get_time()); Serial.print(F(": INFO: Temperatura (LM35DZ): ")); Serial.print( LM35DZ_temp ); Serial.print(F("ºC (")); Serial.print(VALOR); Serial.println(F(")")); }
    if (LM35DZ_temp < 27 && LM35DZ_temp > 20) {
      if ( alarms[TEMP2] != GREEN ) { set_status_led(TEMP2, GREEN); }
      }
      else {
        if ( alarms[TEMP2] != YELLOW) { set_status_led(TEMP2, YELLOW); }
      }    
    }
}

void lerDHT22() {
  // Estado do sensor DHT22 (temperatura e humidade digital):

  //Serial.println(F("DHT22:"));
  sensors_event_t event;  

  // Get temperature event and print its value.
  dht.temperature().getEvent(&event);
  DHT22_temp = event.temperature;
  
  if (isnan(event.temperature)) {
    if ( debug >= 1 ) { Serial.print(get_time()); Serial.println(F(": ERRO: Erro ao ler o sensor DHT22 (temperatura)")); }
  }
  else {
    if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: Temperatura (DHT22): ")); Serial.print(DHT22_temp); Serial.println(F("ºC")); }
    char DHT22_temp_char[5];
    String(DHT22_temp,2).toCharArray(DHT22_temp_char, 6);
    sendzabbix("TEMP0", DHT22_temp_char);
    if (DHT22_temp < 27 && DHT22_temp > 20) {
      if ( alarms[TEMP2] != GREEN ) { set_status_led(TEMP2, GREEN); }
    }
      else {
      if ( alarms[TEMP2] != GREEN ) { set_status_led(TEMP2, YELLOW); }
    }
  }
  
  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  DHT22_humi = event.relative_humidity;
  if (isnan(event.relative_humidity)) {
    if ( debug >= 1) { Serial.print(get_time()); Serial.println(F(": ERRO: Erro ao ler o sensor DHT22 (humidade)")); }
    if ( alarms[TEMP2] != YELLOW ) { set_status_led(TEMP2, YELLOW); }
  }
  else {
    if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: Humidade (DHT22): ")); Serial.print(DHT22_humi); Serial.println(F("%")); }
    char DHT22_humi_char[5];
    String(DHT22_humi,2).toCharArray(DHT22_humi_char, 6);
    sendzabbix("HUMI1", DHT22_humi_char);
    if (DHT22_humi < 80 && DHT22_humi > 20) {
      if ( alarms[TEMP2] != GREEN ) { set_status_led(TEMP2, GREEN); }
    }
      else {
      if ( alarms[TEMP2] != YELLOW ) { set_status_led(TEMP2, YELLOW); }
    }
  }
}

void lerPORTA() {
// Estado da Porta:

  if ( debug >= 2 )
  {
    for (int i=0; i<2; i++) {
      
      Serial.print(get_time()); Serial.print(F(": INFO: PORTA")); Serial.print(i); Serial.print(F(": "));
      if (ESTADO_PORTA[i] == HIGH) {
        Serial.println(F("ABERTA"));
        PORTA_status[i] = "Aberta";
      }
      else {
        Serial.println(F("FECHADA"));
        PORTA_status[i] = "Fechada";
      }  
    }
  }
}

void webserver() {

  // listen for incoming clients
  EthernetClient client = server.available();

  if (client) {
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    boolean primeiraLinha = true;
    String comando = "";
//    int a = 0;
    if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: Acesso via WEB detectado: ")); }
    while ( client.connected() )
    {
      if ( client.available() )
      {
        char c = client.read();
//        if ( a < 1000 )
//        {
//          a++;
//          }
//          else
//          {
//            break;
//          }
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank)
        {
//          Serial.println("\nComando WEB: \"" + comando + "\""); // DEBUG
//          Serial.print("\n"); Serial.print(a); Serial.println(" caracteres.");
          // send a standard http response header
          String webHeader = F("HTTP/1.1 200 OK");
          String ContentType1 = F("Content-Type: ");
          String ContentType2 = F("text/plain");
          if ( comando.startsWith("GET /TEMP1 ") )
          {
            client.println(webHeader);
            client.println(ContentType1 + ContentType2);
            client.println();
            client.println(DS18B20_temp[0]);
            break;
            }
            else if ( comando.startsWith("GET /TEMP2 ") )
            {
              client.println(webHeader);
              client.println(ContentType1 + ContentType2);
              client.println();
              client.println(DS18B20_temp[1]);
              break;
            }
            else if ( comando.startsWith("GET /HUMI1 ") )
            {
              client.println(webHeader);
              client.println(ContentType1 + ContentType2);
              client.println();
              client.println(DHT22_humi);
              break;
            }
            else if ( comando.startsWith("GET /AMPE1 ") )
            {
              client.println(webHeader);
              client.println(ContentType1 + ContentType2);
              client.println();
              client.println(correnteAMPE1);
              break;
            }
            else if ( comando.startsWith("GET /AMPE2 ") )
            {
              client.println(webHeader);
              client.println(ContentType1 + ContentType2);
              client.println();
              client.println(correnteAMPE2);
              break;
            }
            else if ( comando.startsWith("GET /DOOR1 ") )
            {
              if ( ESTADO_PORTA[0] )
              {
                client.println(webHeader);
                client.println(ContentType1 + ContentType2);
                client.println();
                client.println("OPEN*");
              } else
                {
                  client.println("CLOSE");
                }
              break;
            }
            else if ( comando.startsWith("GET /DOOR2 ") )
            {
              if ( ESTADO_PORTA[1] )
              {
                client.println(webHeader);
                client.println(ContentType1 + ContentType2);
                client.println();
                client.println("OPEN*");
              } else
                {
                  client.println("CLOSE");
                }
              break;
            }
            else if ( comando.startsWith("GET /UPTIME ") )
            {
              client.println(webHeader);
              client.println(ContentType1 + ContentType2);
              client.println();
              client.println(get_time());
              break;
            }
            else
            {
            client.println(webHeader);
            // Output em JSON:
            client.println(ContentType1 + "application/json");
            client.println();
            client.print("{\""); client.print(hostname); client.println("\": {");
            client.println("\"modelo\": \"Zabbix Probe\",");
            client.println("\"versao\": \"0\",");
            client.print("\"uptime\": \""); client.print(get_time()); client.println("\",");
            client.print("\"alarme\": \""); client.print(led_status); client.println("\",");
            client.println("\"sondas\": [");
            String a = F("{ \"id\": \"");
            String b = F("\", \"nome\": \"");
            String c = F("\", \"valor\": \"");
            const String d = F("\", \"tipo\": \"Temperatura\", \"escala\": \"ºC\", \"alarme\": \"");
            String f = F("\" },");
            client.print(a); client.print("TEMP0"); client.print(b); client.print("DHT22-T"); client.print(c); client.print(DHT22_temp);      client.print(d); client.print(""); client.println(f);
            client.print(a); client.print("TEMP1"); client.print(b); client.print("DS18B20"); client.print(c); client.print(DS18B20_temp[0]); client.print(d); client.print(alarms[2]); client.println(f);
            client.print(a); client.print("TEMP2"); client.print(b); client.print("DS18B20"); client.print(c); client.print(DS18B20_temp[1]); client.print(d); client.print(alarms[3]); client.println(f);
            client.print(a); client.print("TEMP3"); client.print(b); client.print("LM35DZ");  client.print(c); client.print(LM35DZ_temp);     client.print(d); client.print(""); client.println(f);
            client.print(a); client.print("HUMI1"); client.print(b); client.print("DHT22-H"); client.print(c); client.print(DHT22_humi);      client.print("\", \"tipo\": \"Humidade\", \"escala\": \"%\", \"alarme\": \""); client.print(""); client.println("\" },");
            d = "";
            const String e = F("\", \"tipo\": \"Corrente\", \"escala\": \"A\", \"alarme\": \"");
            client.print(a); client.print("AMPE1"); client.print(b); client.print("SCT-013");  client.print(c); client.print(correnteAMPE1);  client.print(e); client.print(alarms[4]); client.println(f);
            client.print(a); client.print("AMPE2"); client.print(b); client.print("SCT-013");  client.print(c); client.print(correnteAMPE2);  client.print(e); client.print(alarms[5]); client.println(f);
            e = "";
            const String g = F("\", \"tipo\": \"Estado\", \"escala\": \"Boleana\", \"alarme\": \"");
            client.print(a); client.print("DOOR1"); client.print(b); client.print("SWITCH");  client.print(c); client.print(ESTADO_PORTA[0]);   client.print(g); client.print(alarms[0]); client.println(f);
            client.print(a); client.print("DOOR2"); client.print(b); client.print("SWITCH");  client.print(c); client.print(ESTADO_PORTA[1]);   client.print(g); client.print(alarms[1]); client.println("\" }]");
            client.println("}}");
            break;
            }
          } 
          else
          {
            if ( primeiraLinha ) { comando += c; }
          }
        if (c == '\n')
        {
          // you're starting a new line
          comando += '\0';
          currentLineIsBlank = true;
          primeiraLinha = false;
        } else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
  }
}


void sendzabbix(char key1[], char value1[])
{
// Usage: sendzabbix("TEMP1", DS18B20_temp_char);

//  int hostsize = sizeof(host);
  int key1size = sizeof(key1);
  int value1size = sizeof(value1);
//  int hostencodedsize = base64_enc_len(hostsize);
//  int key1encodedsize = base64_enc_len(key1size);
//  int value1encodedsize = base64_enc_len(value1size);
//  char base64host[hostencodedsize];
//  char base64key1[key1encodedsize];
//  char base64value1[value1encodedsize];
// char base64host[200];
// char base64key1[200];
// char base64value1[200];
 char base64host[30];
 char base64key1[30];
 char base64value1[30];

//smartRACK-F0R0-erm (18) 
//TEMP1 (5)
//20.00 (5)
//c21hcnRSQUNLLUYwUjAtZXJt (24)
//VEVNUDE= (8)
//MjAuMDA= (8)

//<req>
//  <host>c21hcnRSQUNLLUYwUjAtZXJt</host>
//  <key>VEVNUDE=</key>
//  <data>MjIuMzE=</data>
//</req>
//OK

//  Debug
//   Serial.println();
//   Serial.println(F("<req>"));
//   Serial.print(F("  <host>"));
//   Serial.print(host); Serial.print(" - "); Serial.print(hostsize);
//   Serial.println(F("</host>"));
//   Serial.print(F("  <key>"));
//   Serial.print(key1); Serial.print(" - "); Serial.print(key1size);
//   Serial.println(F("</key>"));
//   Serial.print(F("  <data>"));
//   Serial.print(value1); Serial.print(" - "); Serial.print(value1size);
//   Serial.println(F("</data>"));
//   Serial.println(F("</req>"));
   
 if (zabclient.connect(zabbix,10051))
  {
//   base64_encode(base64host , host , hostsize );
//   base64_encode(base64key1 , key1 , key1size );
//   base64_encode(base64value1 , value1 , value1size );
   base64_encode(base64host , hostname , 18 );
//   base64_encode(base64host , host , 18 );
   base64_encode(base64key1 , key1 , 5 );
   base64_encode(base64value1 , value1 , 5 );

   if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: Connected with zabbix , sending info...")); }

//  Debug
//   Serial.println();
//   Serial.println(F("<req>"));
//   Serial.print(F("  <host>"));
//   Serial.print(base64host);
//   Serial.println(F("</host>"));
//   Serial.print(F("  <key>"));
//   Serial.print(base64key1);
//   Serial.println(F("</key>"));
//   Serial.print(F("  <data>"));
//   Serial.print(base64value1);
//   Serial.println(F("</data>"));
//   Serial.println(F("</req>"));

   // Com encoding
   zabclient.write("<req>\n");
   zabclient.write("  <host>");
   zabclient.print(base64host);
   zabclient.write("</host>\n");
   zabclient.write("  <key>");
   zabclient.print(base64key1);
   zabclient.write("</key>\n");
   zabclient.write("  <data>");
   zabclient.print(base64value1);
   zabclient.write("</data>\n");
   zabclient.write("</req>\n");
   delay(1);

//   // SEM encoding
//   zabclient.write("<req>\n");
//   zabclient.write("  <host>");
//   zabclient.write(host);
//   zabclient.write("</host>\n");
//   zabclient.write("  <key>");
//   zabclient.print(key1);
//   zabclient.write("</key>\n");
//   zabclient.write("  <data>");
//   zabclient.write(value1);
//   zabclient.write("</data>\n");
//   zabclient.write("</req>\n");
//   delay(1);

   zabclient.stop();
   if ( debug >= 2 ) { Serial.println(F(" done.")); }
   }
   else {
    if ( debug >= 1 ) { Serial.print(get_time()); Serial.print(F(": ERRO: Não foi possivel abrir a porta no servidor: ")); Serial.println(zabbix); }
   }
}

void verifica_portas()
{
  for (int i=0; i<2; i++) {
    char DOOR_ID_char[5];
    String a = "DOOR" + String(i+1);
    a.toCharArray(DOOR_ID_char, 6);
    ESTADO_PORTA[i] = digitalRead(PORTA_PIN[i]);
    if (ESTADO_PORTA[i] != ESTADO_PORTA_ANTERIOR[i]) {
      if (ESTADO_PORTA[i] == HIGH) {
        if ( debug >= 0 ) { Serial.print(get_time()); Serial.print(F(": ALERTA: A porta ")); Serial.print(i); Serial.println(F(" foi aberta.")); }
        sendzabbix(DOOR_ID_char, "OPEN*");
        if ( alarms[DOOR[i]] != RED ) { set_status_led(DOOR[i], RED); }
      }
      else {
        if ( debug >= 0 ) { Serial.print(get_time()); Serial.print(F(": ALERTA: A porta ")); Serial.print(i); Serial.println(F(" foi fechada novamente.")); }
        sendzabbix(DOOR_ID_char, "CLOSE");
        if ( alarms[DOOR[i]] != GREEN ) { set_status_led(DOOR[i], GREEN); }
      }
      ESTADO_PORTA_ANTERIOR[i] = ESTADO_PORTA[i];
    }
  }
}

void pausa(unsigned long int tempo, boolean tipo) {
  // Tempo: Número de segundos para a pausa
  // Tipo: TRUE-> com output "...", FALSE sem output
  
      char a[4] = { '|', '/', '-', '\\' };
      unsigned long int t = tempo * 1000;
      unsigned long int t1 = millis();
      unsigned long int t2 = t1;
      if ( debug >= 2 ) { if ( tipo ) { Serial.print(get_time()); Serial.print(F(": INFO: Pausa por ")); Serial.print(tempo); Serial.println(F("seg... ")); } }
      //if ( tipo ) { syslog(0, "Pausa por " + String(tempo) + "seg... "); }
      while ( t2 - t1 < t )
      {
        for (int i=0; i<4; i++ ) 
        {
          webserver();
          digitalWrite(LED_STATUS_PIN,LOW);
          delay(250);
          verifica_portas();          
          digitalWrite(LED_STATUS_PIN,HIGH);
          delay(250);
          t2 = millis();
          if ( debug >= 2 ) { if ( tipo ) { Serial.print(a[i]); Serial.write(8); } }
        }
      }
      //if ( debug >= 2 ) { if ( tipo ) { Serial.print(" "); } }
}

void loop() {

  digitalWrite(LED_STATUS_PIN,LOW);
  lerDS18B20();
  digitalWrite(LED_STATUS_PIN,HIGH);
  pausa(1, false);
 
  digitalWrite(LED_STATUS_PIN,LOW);
  lerLM35DZ();
  digitalWrite(LED_STATUS_PIN,HIGH);
  pausa(1, false);

  digitalWrite(LED_STATUS_PIN,LOW);
  lerDHT22();
  digitalWrite(LED_STATUS_PIN,HIGH);
  pausa(1, false);

  digitalWrite(LED_STATUS_PIN,LOW);
  lerPORTA();
  digitalWrite(LED_STATUS_PIN,HIGH);
  pausa(1, false);

  digitalWrite(LED_STATUS_PIN,LOW);
  lerPincasAmp();
  digitalWrite(LED_STATUS_PIN,HIGH);
  //pausa(1, false);

  pausa(60, true);  
}

//void setup() {
//
//  Pinos do Relé
//  pinMode(powerPin[0], OUTPUT);
//  pinMode(powerPin[1], OUTPUT);
//  pinMode(powerPin[2], OUTPUT);
//  pinMode(powerPin[3], OUTPUT);
//
//  Inicializa os relés com base no estado que está definido no 'array' "rele"
//  digitalWrite(powerPin[0], rele[0]);
//  digitalWrite(powerPin[1], rele[1]);
//  digitalWrite(powerPin[2], rele[2]);
//  digitalWrite(powerPin[3], rele[3]);
//}

//void syslog(int tipo, String mensagem)
//{
//  Serial.println(); Serial.print(get_time()); Serial.print(F(": "));
//  switch (tipo)
//  {
//    case 0:
//      Serial.print(F("INFO"));
//      break;
//    case 1:
//      Serial.print(F("ERRO"));
//      break;
//    case 2:
//      Serial.print(F("ALARME"));
//      break;
//    default:
//      Serial.print(F("*ERRO no codigo*"));
//  }
//  Serial.print(F(": ")); Serial.print(mensagem);
//}

//void setPower(int porta, boolean estado)
//{
//  if ( rele[porta] != estado )
//  {
//    rele[porta] = !rele[porta];
//    digitalWrite(powerPin[porta], rele[porta]);
//    if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: Tomada #")); Serial.print(porta + 1); Serial.print(F(" mudou o estado para ")); if ( rele[porta] ) { Serial.println(F("ligada.")); } else { Serial.println(F("desligada.")); } }
//    }
//}


//            else if ( comando.startsWith("GET /TEMP2 ") )
//            {
//              client.println(webHeader);
//              client.println(ContentType1 + ContentType2);
//              client.println();
//              client.println(DHT22_temp);
//              break;
//            }
//            else if ( comando.startsWith("GET /TEMP3 ") )
//            {
//              client.println(webHeader);
//              client.println(ContentType1 + ContentType2);
//              client.println();
//              client.println(LM35DZ_temp);
//              break;
//            }
//            else if ( comando.startsWith("GET /UNLOCK1 ") )
//            {
//              if ( debug >= 2 ) { Serial.print(get_time()); Serial.print(F(": INFO: A abrir a porta #1 (LOCK1)...")); }
//              setPower(0, true);
//              client.println(webHeader);
//              client.println(ContentType1 + ContentType2);
//              client.println();
//              client.println("OK");
//              pausa(5, false);
//              setPower(0, false);
//              break;
//            }

// Output em HTML:
//          client.println("Content-Type: text/html");
//          client.println();
//          client.println("<head>");
//          client.println("<meta http-equiv=\"refresh\" content=\"60\">");
//          client.println("</head>");
//          client.println("<body>");
//          client.println("<font face=\"Courier\" size=2>");
//          
//          client.println("Temperatura: <br />");
//          client.print("&nbsp;LM35DZ: &nbsp;"); client.print(LM35DZ_temp); client.print("&#186;C"); client.println("<br />");
//          client.print("&nbsp;DHT22: &nbsp; "); client.print(DHT22_temp); client.print("&#186;C"); client.println("<br />");
//          client.print("&nbsp;DS18B20: "); client.print(DS18B20_temp); client.print("&#186;C"); client.println("<br /><br />");
//          client.print("Humidade: "); client.print(DHT22_humi); client.println("%<br /><br />");
//          client.print("Porta: "); client.print(PORTA1_status); client.println("</font>");
//          client.println("</body>");


// Output em XML:
//          client.println("Content-Type: text/xml");
//          client.println();
//          client.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
//          client.print("<smartRACK uptime=\""); client.print(get_time()); client.println("\">");
//          client.println("   <sonda id=\"DS18B20\">");
//          client.print("      <valor>"); client.print(DS18B20_temp); client.println("</valor>");
//          client.println("      <tipo>Temperatura</tipo>");
//          client.println("      <escala>Graus Centigrados</escala>");
//          client.println("   </sonda>");
//          client.println("   <sonda id=\"LM35DZ\">");
//          client.print("      <valor>"); client.print(LM35DZ_temp); client.println("</valor>");
//          client.println("      <tipo>Temperatura</tipo>");
//          client.println("      <escala>Graus Centigrados</escala>");
//          client.println("   </sonda>");
//          client.println("   <sonda id=\"DHT22-T\">");
//          client.print("      <valor>"); client.print(DHT22_temp); client.println("</valor>");
//          client.println("      <tipo>Temperatura</tipo>");
//          client.println("      <escala>Graus Centigrados</escala>");
//          client.println("   </sonda>");
//          client.println("   <sonda id=\"DHT22-H\">");
//          client.print("      <valor>"); client.print(DHT22_humi); client.println("</valor>");
//          client.println("      <tipo>Humidade</tipo>");
//          client.println("      <escala>Percentagem</escala>");
//          client.println("   </sonda>");
//          client.println("   <sonda id=\"PORTA1\">");
////          client.print("      <valor>"); client.print(PORTA1_status); client.println("</valor>");
//          client.print("      <valor>"); client.print(ESTADO_PORTA1); client.println("</valor>");
//          client.println("      <tipo>Status</tipo>");
//          client.println("      <escala>Boleana</escala>");
//          client.println("   </sonda>");
//          client.println("</smartRACK>");

