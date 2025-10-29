// ESP8266 CODE V2.3 - Segurança, Carrossel, e Comunicação Serial Corrigida para Arduino Mega

#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// --- CONFIGURAÇÕES DE SEGURANÇA ---
#define SECRET_TOKEN "tk-mimi-secreto-ab12cd34ef56" 
#define MAX_REQ_LINE_SIZE 512 

// --- CONFIGURAÇÕES DE VALIDAÇÃO DE ENTRADA ---
#define MIN_FEED_TIME 1
#define MAX_FEED_TIME 24 
#define MIN_FEED_QUANTITY 0
#define MAX_FEED_QUANTITY 5000

// --- CONFIGURAÇÕES DE IMAGEM DO CARROSSEL (Use seus links permanentes do Imgur) ---
const char* FOTO_URL_1 = "https://i.imgur.com/p4H6LQ0.jpg";
const char* FOTO_URL_2 = "https://i.imgur.com/d4HXD8e.jpg";
const char* FOTO_URL_3 = "https://preview.redd.it/28cm0nvgwi681.jpg?width=640&crop=smart&auto=webp&s=daa957209edf778a8037e7a331712166dab71b24";
// ---------------------------------------------------------------------

const char *ssid      = "Mojo Dojo Casa House";
const char *password = "rockstaRxD0123";
const long utcOffsetInSeconds = -10800;

WiFiServer server(80);

char requestLine[MAX_REQ_LINE_SIZE]; 

int feedTime;
int feedQuantity;
unsigned long currentTime = millis();
unsigned long previousTime = 0;
const long timeoutTime = 2000;
int lastMinute;
int lastHour;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

void sendForbidden(WiFiClient client) {
  client.println("HTTP/1.1 403 Forbidden");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE html><html><body><h1>403 Acesso Negado</h1><p>Token Invalido.</p></body></html>");
}

void setup(){
  feedTime = 4; 
  feedQuantity = 900;
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
    // NÃO ENVIA NADA AQUI (Apenas no loop principal de comando)
    lastMinute = minute;
  }
}

void loop() {
  WiFiClient client = server.available();
  updateTime();
  
  if (client) {
    updateTime();
    
    // --- LÓGICA DE SEGURANÇA CONTRA DOS POR MEMÓRIA ---
    String requestString = "";
    int charCount = 0;
    
    memset(requestLine, 0, MAX_REQ_LINE_SIZE); 
    
    while (client.connected() && (millis() - previousTime <= timeoutTime)) {
      if (client.available()) {
        char c = client.read();
        
        if (charCount < MAX_REQ_LINE_SIZE - 1) { 
          requestLine[charCount++] = c;
        }
        
        if (c == '\n') {
          requestLine[charCount] = '\0'; 
          requestString = String(requestLine);
          break;
        } else if (c == '\r') {
          // Ignora CR
        }
      }
      if (previousTime == 0) previousTime = millis(); 
    }

    // --- SEGURANÇA: CONTROLE DE ACESSO ---
    bool isAuthenticated = requestString.indexOf(SECRET_TOKEN) != -1;
    
    if ( (requestString.indexOf("/feedTime/") >= 0 || 
          requestString.indexOf("/feedQuantity/") >= 0 || 
          requestString.indexOf("/feed/now") >= 0) && !isAuthenticated) {
      
      Serial.println("ALERTA: Tentativa de acesso crítico sem token.");
      sendForbidden(client);
      client.stop();
      return;
    }

    // --- INÍCIO DA RESPOSTA HTTP ---
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println("Connection: close");
    client.println();

    // 1. Lógica de Validação e Execução
    if (requestString.indexOf("/feedTime/") >= 0) {
        int valStartIndex = requestString.indexOf("/feedTime/") + 10; 
        String valString = requestString.substring(valStartIndex, valStartIndex + 2); 
        int tempFeedTime = valString.toInt();

        // VALIDAÇÃO DE ENTRADA (MANTIDA)
        if (tempFeedTime >= MIN_FEED_TIME && tempFeedTime <= MAX_FEED_TIME) {
            feedTime = tempFeedTime;
            // CORREÇÃO: ENVIO SERIAL NO FORMATO ESPERADO PELO MEGA
            Serial.println("TIME=" + String(feedTime)); 
        } else {
            Serial.println("ALERTA: Valor de feedTime REJEITADO: " + String(tempFeedTime));
        }
    }
    
    if (requestString.indexOf("/feedQuantity/") >= 0) {
        int valStartIndex = requestString.indexOf("/feedQuantity/") + 14; 
        String valString = requestString.substring(valStartIndex, valStartIndex + 4);
        int tempFeedQuantity = valString.toInt();

        // VALIDAÇÃO DE ENTRADA (MANTIDA)
        if (tempFeedQuantity >= MIN_FEED_QUANTITY && tempFeedQuantity <= MAX_FEED_QUANTITY) {
            feedQuantity = tempFeedQuantity;
            // CORREÇÃO: ENVIO SERIAL NO FORMATO ESPERADO PELO MEGA
            Serial.println("FEED_QUANTITY=" + String(feedQuantity));
        } else {
            Serial.println("ALERTA: Valor de feedQuantity REJEITADO: " + String(tempFeedQuantity));
        }
    }
    
    if (requestString.indexOf("/feed/now") >= 0) {
      // CORREÇÃO: ENVIO SERIAL NO FORMATO ESPERADO PELO MEGA
      Serial.println("FEED_NOW=" + timeClient.getFormattedTime());
    }

    // 2. Conteúdo da Página HTML (Carrossel e Botões)
    client.println("<!DOCTYPE html><html>");
    client.println("<meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\">");
    client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    client.println("<link rel=\"icon\" href=\"data:,\">");
    client.println("<style>");
    
    // ESTILOS GERAIS
    client.println("html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
    client.println("h1,p {font-weight: bold;color: #126e54; font-size: 32px;}");
    client.println("p {font-size: 16px;}");
    client.println(".button { background-color: #1BAE85; border: none; color: white; padding: 7px 14px;");
    client.println("text-decoration: none; font-size: 12px; margin: 2px; cursor: pointer;}");
    
    // --- ESTILOS DO CARROSSEL ---
    client.println(".carousel-container { width: 300px; height: 200px; margin: 20px auto; overflow: hidden; border: 2px solid #1BAE85; }");
    client.println(".slide-track { display: flex; width: 300%; animation: slide-animation 10s infinite; }");
    client.println(".slide { width: 33.3333%; height: 200px; flex-shrink: 0; background-size: cover; background-position: center; }");

    client.println("@keyframes slide-animation {");
    client.println("  0% { transform: translateX(0); }");
    client.println("  33% { transform: translateX(-100%); }");
    client.println("  66% { transform: translateX(-200%); }");
    client.println("  100% { transform: translateX(0); }");
    client.println("}");
    // FIM DOS ESTILOS DO CARROSSEL

    client.println("</style></head>");

    client.println("<body><h1>Alimentador Mimi WiFi</h1>");
    
    // --- INSERÇÃO DO CARROSSEL COM LINKS EXTERNOS ---
    client.println("<div class=\"carousel-container\">");
    client.println("<div class=\"slide-track\">");
    
    client.print("<div class=\"slide\" style=\"background-image: url('");
    client.print(FOTO_URL_1);
    client.println("');\"></div>");

    client.print("<div class=\"slide\" style=\"background-image: url('");
    client.print(FOTO_URL_2);
    client.println("');\"></div>");
    
    client.print("<div class=\"slide\" style=\"background-image: url('");
    client.print(FOTO_URL_3);
    client.println("');\"></div>");
    
    client.println("</div></div>");
    // --- FIM DO CARROSSEL ---

    client.println("<p>Horário de funcionamento: 07:00 até as 20:00</p>");
    client.println("<p>Tempo entre as refeições: " + String(feedTime) + " horas</p>");
    client.println("<p>Tempo despejando a ração: " + String(feedQuantity) + " milisegundos</p>");
    
    // LINKS DE COMANDO COM O TOKEN DE SEGURANÇA
    client.println("<p><a href=\"/" SECRET_TOKEN "/feed/now\"><button class=\"button\">Alimentar Agora</button></a></p>");
    client.println("<p><input type=\"text\" style=\"width: 40px\" name=\"textQuantityBox\" id=\"textQuantityBox\" class=\"button\" value=\"\"/><a href=\"\" onclick=\"this.href='/" SECRET_TOKEN "/feedQuantity/'+document.getElementById('textQuantityBox').value\"><button class=\"button\">Definir quantidade de ração (ms)</button></a></p>");
    client.println("<p><input type=\"text\" style=\"width: 40px\" name=\"textTimeBox\" id=\"textTimeBox\" class=\"button\" value=\"\"/><a href=\"\" onclick=\"this.href='/" SECRET_TOKEN "/feedTime/'+document.getElementById('textTimeBox').value\"><button class=\"button\">Definir tempo entre as refeições (H)</button></a></p>");
    
    client.println("</body></html>");
    client.println();
    
    memset(requestLine, 0, MAX_REQ_LINE_SIZE);
    
    client.stop();
  }
}