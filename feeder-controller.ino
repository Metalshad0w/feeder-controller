#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>  
#include <NTPClient.h> 
#include <WiFiUdp.h>   

// ******************************************************
// --- CREDENCIAIS DE WiFi ---
// ******************************************************
const char* SSID_NAME = "Mojo Dojo Casa House"; 
const char* PASSWORD = "rockstaRxD0123"; 
// ******************************************************

// --- Configurações de Segurança e Lógica ---
const String SECRET_TOKEN = "COMIDA"; 
int FEED_INTERVAL_HOURS = 4;        
int FEED_DURATION_MS = 900;          

// --- Variáveis de Estado ---
unsigned long lastFeedTime = 0; 
int lastMinute = -1;
// Variáveis globais de alerta/token REMOVIDAS (V7.16)
ESP8266WebServer server(80);

// NTP Client Setup
const long utcOffsetInSeconds = -10800;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds); 

// --- Protótipos de Funções ---
void send_time_interval(int value);
void send_feed_duration(int value);
void send_feed_now();

void handleRoot();
void handleFeedNow();
void handleFeedTime();
void handleFeedQuantity();
void handleNotFound();
void serveIndexHtml(String token, String alert, int fTime, int fQty);
void updateTime();
void send_feedback_and_refresh(String alert_message, String token_value, int fTime, int fQty); // NOVO PROTÓTIPO

// ----------------------------------------------------------------------
// FUNÇÕES DE TEMPO E COMUNICAÇÃO SERIAL
// ----------------------------------------------------------------------

void updateTime(){
    int minute = timeClient.getFormattedTime().substring(3,5).toInt();
    if(minute != lastMinute){
        timeClient.update();
        Serial.println(timeClient.getFormattedTime());
        lastMinute = minute;
    }
}
void send_time_interval(int value) {
    String command = "TIME=" + String(value);
    Serial.println(command);
}
void send_feed_duration(int value) {
    String command = "FEED_QUANTITY=" + String(value);
    Serial.println(command);
}
void send_feed_now() {
    timeClient.update(); 
    String command = "FEED_NOW=" + timeClient.getFormattedTime();
    Serial.println(command);
}

// ----------------------------------------------------------------------
// FUNÇÃO PRINCIPAL: SETUP
// ----------------------------------------------------------------------

void setup() {
    Serial.begin(115200); 
    
    if (!LittleFS.begin()) {
        // Falha ao montar o LittleFS.
    }
    
    // --- Conexão WiFi ---
    WiFi.begin(SSID_NAME, PASSWORD);

    int maxTries = 30;
    while (WiFi.status() != WL_CONNECTED && maxTries > 0) {
        delay(500);
        yield(); 
        maxTries--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        timeClient.begin();
        timeClient.update();
        updateTime(); 
    }
    
    // --- Configuração das Rotas do Servidor ---
    server.on("/", handleRoot);
    server.on("/feed/now", handleFeedNow);
    server.on("/feedTime", handleFeedTime);        
    server.on("/feedQuantity", handleFeedQuantity); 
    
    server.on("/favicon.ico", []() {
        server.send(204); 
    });

    server.onNotFound(handleNotFound);

    server.begin();
    
    lastFeedTime = millis();
    
    send_time_interval(FEED_INTERVAL_HOURS);
    send_feed_duration(FEED_DURATION_MS);
}

// ----------------------------------------------------------------------
// FUNÇÃO PRINCIPAL: LOOP
// ----------------------------------------------------------------------

void loop() {
    server.handleClient();
    
    if (WiFi.status() == WL_CONNECTED) {
        updateTime();
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
    }
    
    if (millis() - lastFeedTime >= (unsigned long)FEED_INTERVAL_HOURS * 3600000UL) {
        
        // 1. Obtém a hora atual (somente a hora em formato 24h)
        int currentHour = timeClient.getHours();
        
        // 2. Verifica se a hora está no intervalo [7, 20)
        // O intervalo é de 07:00 (inclusivo) até 19:59:59 (exclusivo de 20:00)
        if (currentHour >= 7 && currentHour < 20) {
            
            // 3. Dispara a alimentação (Comentado pq está sendo gerenciada pelo MEGA)
            //send_feed_now(); 
            
            // 4. Reinicia a contagem
            lastFeedTime = millis();
        }
        // Se o tempo passou, mas está fora do horário (20h às 6h), o 'lastFeedTime' 
        // NÃO é atualizado. O sistema espera até que o 'currentHour' seja 7h novamente
        // para disparar a alimentação e reiniciar o ciclo.
    }
}

// ----------------------------------------------------------------------
// FUNÇÕES DE SERVIDOR E ROTEAMENTO
// ----------------------------------------------------------------------

String getNextFeedTimeDisplay(unsigned long lastFeed, int intervalHours) {
    // ... (Mantém a lógica de cálculo de tempo) ...
    unsigned long nextFeedMillis = lastFeed + (unsigned long)intervalHours * 3600000UL;
    unsigned long diff = nextFeedMillis - millis();

    if (diff > 0) {
        long seconds = diff / 1000;
        long minutes = seconds / 60;
        long hours = minutes / 60;
        
        minutes %= 60; 

        String result = "";
        
        if (hours > 0) {
            result += String(hours) + "h ";
        }
        
        result += String(minutes) + "m";
        
        return "Em " + result;
    } else {
        return "Aguarde, calculando...";
    }
}

// NOVO: Função para exibir a mensagem temporariamente e redirecionar via Meta-Refresh
void send_feedback_and_refresh(String alert_message, String token_value, int fTime, int fQty) {
    String html = "<!DOCTYPE html><html><head>";
    // ALTERADO V7.17: O Meta-Refresh agora recarrega para a rota raiz após 5 segundos
    html += "<meta http-equiv=\"refresh\" content=\"5; url=/\">"; 
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<title>Processando...</title>";
    html += "<style>";
    html += "body{font-family:'Arial', sans-serif;margin:0;padding:20px;background-color:#f8f9fa;color:#495057; text-align: center;}";
    html += ".alert-success, .alert-warning{padding:12px;border-radius:6px;margin-bottom:15px;font-weight:bold; display: inline-block;}";
    html += ".alert-success{background-color:#d4edda;color:#155724;border:1px solid #c3e6cb;}";
    html += ".alert-warning{background-color:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}";
    html += "</style></head><body>";
    
    // Alerta HTML
    String alertClass = (alert_message.startsWith("SUCESSO")) ? "alert-success" : "alert-warning";
    html += "<div class=\"" + alertClass + "\"><p>";
    html += alert_message; 
    html += "</p></div>";
    
    html += "<p>Redirecionando em 5 segundos...</p>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}

void serveIndexHtml(String token, String alert, int fTime, int fQty) {
    File file = LittleFS.open("/index.html", "r"); 
    if (!file) {
        server.send(500, "text/plain", "ERRO: index.html nao encontrado. (Alimentador Mimi WiFi)");
        return;
    }

    String htmlContent = file.readString();
    file.close(); 
    
    // --- Lógica de Alerta (Não usada mais, mas mantida por consistência) ---
    String alertString = "";
    // O Alerta aqui sempre será vazio, pois o feedback é feito pela página intermediária
    
    // --- Substituições Dinâmicas ---
    String nextTimeDisplay = getNextFeedTimeDisplay(lastFeedTime, fTime);

    // Token é SEMPRE vazio para a página principal (não mantemos mais o valor incorreto)
    htmlContent.replace("[LAST_TOKEN_VALUE]", ""); 
    
    htmlContent.replace("[FEED_TIME]", String(fTime));
    htmlContent.replace("[FEED_QUANTITY]", String(fQty));
    htmlContent.replace("[NEXT_FEED_TIME]", nextTimeDisplay); 
    
    // Substitui o placeholder de alerta por uma string vazia
    htmlContent.replace("", alertString);

    // --- Envio do Conteúdo (Prevenção de Cache) ---
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    
    server.send(200, "text/html", htmlContent); 
}


// ROTA: / (Página Inicial) - SIMPLES
void handleRoot() {
    // Alerta e Token vazios, pois a página intermediária lida com o feedback
    serveIndexHtml("", "", FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: /feed/now 
void handleFeedNow() {
    String token = server.arg("token");
    String alert = "";
    
    String upperToken = token;
    upperToken.toUpperCase();

    if (upperToken == SECRET_TOKEN) {
        send_feed_now(); 
        lastFeedTime = millis();
        alert = "SUCESSO: Alimento dispensado manualmente! Comando enviado: FEED_NOW"; 
    } else {
        alert = "ERRO: Token invalido ou nao fornecido.";
    }

    // Feedback imediato e recarga via Meta-Refresh
    send_feedback_and_refresh(alert, token, FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: /feedTime 
void handleFeedTime() {
    String pathValue = server.arg("value");
    String token = server.arg("token");
    String alert = "";
    int newTime = pathValue.toInt();
    
    String upperToken = token;
    upperToken.toUpperCase();

    if (upperToken != SECRET_TOKEN) { 
        alert = "ERRO: Token invalido ou nao fornecido.";
    } else if (newTime >= 1 && newTime <= 24) {
        FEED_INTERVAL_HOURS = newTime;
        send_time_interval(newTime); 
        alert = "SUCESSO: Intervalo de alimentacao definido para " + String(newTime) + " horas.";
    } else {
        alert = "ERRO: Valor de intervalo deve ser entre 1 e 24 horas.";
    }

    // Feedback imediato e recarga via Meta-Refresh
    send_feedback_and_refresh(alert, token, FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: /feedQuantity 
void handleFeedQuantity() {
    String pathValue = server.arg("value");
    String token = server.arg("token");
    String alert = "";
    int newQty = pathValue.toInt();
    
    String upperToken = token;
    upperToken.toUpperCase();

    if (upperToken != SECRET_TOKEN) { 
        alert = "ERRO: Token invalido ou nao fornecido.";
    } else if (newQty >= 100 && newQty <= 5000) {
        FEED_DURATION_MS = newQty;
        send_feed_duration(newQty); 
        alert = "SUCESSO: Duracao da alimentacao definida para " + String(newQty) + " ms.";
    } else {
        alert = "ERRO: Valor de duracao deve ser entre 100 e 5000 ms.";
    }
    
    // Feedback imediato e recarga via Meta-Refresh
    send_feedback_and_refresh(alert, token, FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: Não Encontrada
void handleNotFound() {
    server.send(404, "text/plain", "404: Nao Encontrado.");
}