//ESO8266 CODE
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

const char *ssid     = "Mojo Dojo Casa House";
const char *password = "rockstaRxD0123";

const long utcOffsetInSeconds = -10800;

WiFiServer server(80);
String header;

int feedTime;
int feedQuantity;
unsigned long currentTime = millis();
unsigned long previousTime = 0;
const long timeoutTime = 2000;
int lastMinute;
int lastHour;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

void setup(){
  feedTime = 2;
  feedQuantity = 100;
  lastMinute = 0;
  Serial.begin(115200);
  delay(5000);
  WiFi.begin(ssid, password);

  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
  }

  timeClient.begin();
  server.begin();
  timeClient.update();
}

void updateTime(){
  int minute = timeClient.getFormattedTime().substring(3,5).toInt();
  if(abs(minute - lastMinute) > 5){
    timeClient.update();
    Serial.println(timeClient.getFormattedTime());
    lastMinute = minute;
  }
}

void loop() {
WiFiClient client = server.available();
updateTime();   // Escute os clientes conectados 
  if (client) {        
    updateTime();                     // Se um novo cliente se conectar
    String currentLine = "";                // Faça uma String para armazenar dados recebidos do cliente
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= timeoutTime) { // loop enquanto cliente estiver conectado.
      currentTime = millis();
      if (client.available()) {             // Se holver bytes para ler do cliente,
        char c = client.read();             // faça a leitura.
        header += c;
        if (c == '\n') {                    // Se o byte é um caractere de nova linha, 
                                            // é o fim da solicitação HTML,entao envie uma resposta.
          if (currentLine.length() == 0) {  
            //Envie um cabeçalho HTTP de resposta.
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
 
            // Procure o trecho "GET /13/on" dentro da solicitação do recebida do cliente.
            if (header.indexOf("GET /feedTime/") >= 0) {
              //Envie um comando para Mega2560 via serial.
              feedTime = header.substring(14, 16).toInt();
              Serial.println("TIME=" + String(feedTime));
            } 
            if (header.indexOf("GET /feedQuantity/") >= 0) {
              //Envie um comando para Mega2560 via serial.
              feedQuantity = header.substring(18, 21).toInt();
              Serial.println("FEED_QUANTITY=" + String(feedQuantity));
            }
            if (header.indexOf("GET /feed/now") >= 0) {
              //Envie um comando para Mega2560 via serial.
              Serial.println("FEED_NOW=" + timeClient.getFormattedTime());
            }
              // Pagina HTML
              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"icon\" href=\"data:,\">");
              // CSS para estilizar a pagina
              client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
              client.println("h1,p {font-weight: bold;color: #126e54; font-size: 32px;}");
              client.println("p {font-size: 16px;}");
              client.println(".button { background-color: #1BAE85; border: none; color: white; padding: 7px 14px;");
              client.println("text-decoration: none; font-size: 12px; margin: 2px; cursor: pointer;}");
              client.println("</style></head>");
 
              client.println("<body><h1>Alimentador Mimi WiFi</h1>");
 
              // Mostre o estado atual do pino 13, aqui representado pela variavel de estado outputState. 
              client.println("<p>Tempo entre as refeicoes: " + String(feedTime) + " horas</p>");
              client.println("<p>Tempo despejando racao: " + String(feedQuantity) + " milisegundos</p>");
              client.println("<p><input type=\"text\" name=\"textTimeBox\" id=\"textTimeBox\" class=\"button\" value=\"\"/><a href=\"\" onclick=\"this.href='/feedTime/'+document.getElementById('textTimeBox').value\"><button class=\"button\">Definir Hora</button></a></p>");
              client.println("<p><input type=\"text\" name=\"textQuantityBox\" id=\"textQuantityBox\" class=\"button\" value=\"\"/><a href=\"\" onclick=\"this.href='/feedQuantity/'+document.getElementById('textQuantityBox').value\"><button class=\"button\">Definir quantidade de racao (ms)</button></a></p>");
              client.println("<p><a href=\"/feed/now\"><button class=\"button\">Alimentar Agora</button></a></p>");
              client.println("</body></html>");
              // A resposta HTTP termina com outra linha em branco.
              client.println();
              break;
            } else { // Se você recebeu uma nova linha, limpe currentLine
              currentLine = "";
            }
          } else if (c != '\r') {  // Se você tiver mais alguma coisa além de um caractere de retorno de carro,
            currentLine += c;      // Adicione-o ao final do currentLine.
          }
        }
        updateTime();
      }
      // Limpe a variável de cabeçalho
      header = "";
      // Feche a conexão.
      client.stop();
      updateTime();
    }
  updateTime();
}