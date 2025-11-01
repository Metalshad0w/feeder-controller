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
const String SECRET_TOKEN = "COMIDA"; // Token em maiúsculas
int FEED_INTERVAL_HOURS = 4;        
int FEED_DURATION_MS = 900;          

// --- CLEAN V8.0: Constantes de Tempo (Sem "Números Mágicos") ---
const int START_HOUR = 7; // 7:00 AM
const int END_HOUR = 20;  // 20:00 PM (horário de parada, exclusivo)
const unsigned long ONE_SECOND_MS = 1000UL;
const unsigned long ONE_MINUTE_MS = 60 * ONE_SECOND_MS;
const unsigned long ONE_HOUR_MS = 60 * ONE_MINUTE_MS; // = 3600000UL

// --- Variáveis de Estado ---
unsigned long lastFeedTime = 0; 
int lastMinute = -1;
ESP8266WebServer server(80);

// NTP Client Setup (Timezone do Brasil: -3 horas)
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
void send_feedback_and_refresh(String alert_message, String token_value, int fTime, int fQty);
String getNextFeedTimeDisplay(unsigned long lastFeed, int intervalHours);
bool isTokenValid(String token); // CLEAN V8.0: Nova função helper

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
    
    // Previne race condition
    server.on("/favicon.ico", []() {
        server.send(204); 
    });

    server.onNotFound(handleNotFound);

    server.begin();
    
    lastFeedTime = millis();
    
    // Sincroniza configurações iniciais com o controlador do motor
    send_time_interval(FEED_INTERVAL_HOURS);
    send_feed_duration(FEED_DURATION_MS);
}

// ----------------------------------------------------------------------
// FUNÇÃO PRINCIPAL: LOOP (CLEAN V8.0 - Usa constantes de tempo)
// ----------------------------------------------------------------------

void loop() {
    server.handleClient();
    
    if (WiFi.status() == WL_CONNECTED) {
        updateTime();
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
    }
    
    // Lógica de Alimentação Automática (Checagem de tempo e horário)
    // CLEAN V8.0: Usa a constante ONE_HOUR_MS
    if (millis() - lastFeedTime >= (unsigned long)FEED_INTERVAL_HOURS * ONE_HOUR_MS) {
        
        int currentHour = timeClient.getHours();
        
        // CLEAN V8.0: Usa as constantes START_HOUR e END_HOUR
        if (currentHour >= START_HOUR && currentHour < END_HOUR) {
            
            //send_feed_now(); 
            lastFeedTime = millis();
        }
    }
}

// ----------------------------------------------------------------------
// FUNÇÕES DE SERVIDOR E ROTEAMENTO
// ----------------------------------------------------------------------

// CLEAN V8.0: Nova função helper para validação de token (Evita repetição)
bool isTokenValid(String token) {
    token.toUpperCase(); // Converte a cópia local da string
    return (token == SECRET_TOKEN);
}

// FUNÇÃO CRUCIAL (CLEAN V8.0 - Usa constantes de tempo)
String getNextFeedTimeDisplay(unsigned long lastFeed, int intervalHours) {
    unsigned long nextFeedMillis = lastFeed + (unsigned long)intervalHours * ONE_HOUR_MS;
    unsigned long now = millis();
    
    unsigned long diff;
    
    // --- Lógica de Bloqueio Noturno (CLEAN V8.0) ---
    if (nextFeedMillis <= now) {
        int currentHour = timeClient.getHours();
        
        // Se o tempo expirou E estamos na janela de bloqueio (CLEAN V8.0)
        if (currentHour >= END_HOUR || currentHour < START_HOUR) {
            
            // Calcula o tempo restante até START_HOUR
            long targetSeconds = START_HOUR * 3600; 
            long currentSeconds = timeClient.getHours() * 3600 + timeClient.getMinutes() * 60 + timeClient.getSeconds();
            long secondsInDay = 24 * 3600;
            long secondsUntilNextFeed = 0;
            
            if (currentSeconds < targetSeconds) {
                secondsUntilNextFeed = targetSeconds - currentSeconds;
            } else {
                secondsUntilNextFeed = (secondsInDay - currentSeconds) + targetSeconds;
            }

            long h = secondsUntilNextFeed / 3600;
            long m = (secondsUntilNextFeed % 3600) / 60;

            String result = "";
            if (h > 0) result += String(h) + "h ";
            result += String(m) + "m";
            return "Em " + result; 
        }
    }
    // --- Fim da Lógica de Bloqueio Noturno ---
    
    // --- Countdown Normal ---
    if (nextFeedMillis <= now) {
        diff = 0; 
    } else {
        diff = nextFeedMillis - now;
    }

    long seconds = diff / ONE_SECOND_MS;
    long minutes = seconds / 60;
    long hours = minutes / 60;
    minutes %= 60; 

    String result = "";
    if (hours > 0) result += String(hours) + "h ";
    result += String(minutes) + "m";
    return "Em " + result;
}

// Função de Feedback (Meta-Refresh)
void send_feedback_and_refresh(String alert_message, String token_value, int fTime, int fQty) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta http-equiv=\"refresh\" content=\"5; url=/\">"; // 5 segundos
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<title>Processando...</title>";
    html += "<style>";
    html += "body{font-family:'Arial', sans-serif;margin:0;padding:20px;background-color:#f8f9fa;color:#495057; text-align: center;}";
    html += ".alert-success, .alert-warning{padding:12px;border-radius:6px;margin-bottom:15px;font-weight:bold; display: inline-block;}";
    html += ".alert-success{background-color:#d4edda;color:#155724;border:1px solid #c3e6cb;}";
    html += ".alert-warning{background-color:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}";
    html += "</style></head><body>";
    
    String alertClass = (alert_message.startsWith("SUCESSO")) ? "alert-success" : "alert-warning";
    html += "<div class=\"" + alertClass + "\"><p>";
    html += alert_message; 
    html += "</p></div>";
    
    html += "<p>Redirecionando em 5 segundos...</p>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}


// OTIMIZAÇÃO V8.0: Esta função agora lê linha por linha (Stream)
// Isso economiza MUITA RAM e previne crashes de "Out of Memory".
void serveIndexHtml(String token, String alert, int fTime, int fQty) {
    File file = LittleFS.open("/index.html", "r"); 
    if (!file) {
        server.send(500, "text/plain", "ERRO: index.html nao encontrado. (Alimentador Mimi WiFi)");
        return;
    }

    // --- Envio Otimizado (Stream) ---
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    
    // Inicia a resposta em modo "chunked" (streaming)
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", ""); // Envia cabeçalhos

    String nextTimeDisplay = getNextFeedTimeDisplay(lastFeedTime, fTime);

    String line;
    while (file.available()) {
        line = file.readStringUntil('\n');

        // Substituições em tempo real (muito eficiente em memória)
        line.replace("[LAST_TOKEN_VALUE]", ""); // Sempre vazio na V8.0
        line.replace("[FEED_TIME]", String(fTime));
        line.replace("[FEED_QUANTITY]", String(fQty));
        line.replace("[NEXT_FEED_TIME]", nextTimeDisplay); 
        line.replace("", ""); // Alerta é tratado pelo meta-refresh

        server.sendContent(line + "\n"); // Envia a linha processada
        yield(); // Essencial para estabilidade
    }

    file.close();
    server.sendContent(""); // Finaliza a resposta chunked
}

// ROTA: / (Página Inicial)
void handleRoot() {
    serveIndexHtml("", "", FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: /feed/now (CLEAN V8.0: Usa isTokenValid())
void handleFeedNow() {
    String token = server.arg("token");
    String alert = "";
    
    if (isTokenValid(token)) {
        send_feed_now(); 
        lastFeedTime = millis();
        alert = "SUCESSO: Alimento dispensado manualmente! Comando enviado: FEED_NOW"; 
    } else {
        alert = "ERRO: Token invalido ou nao fornecido.";
    }

    send_feedback_and_refresh(alert, token, FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: /feedTime (CLEAN V8.0: Usa isTokenValid())
void handleFeedTime() {
    String pathValue = server.arg("value");
    String token = server.arg("token");
    String alert = "";
    int newTime = pathValue.toInt();
    
    if (!isTokenValid(token)) { 
        alert = "ERRO: Token invalido ou nao fornecido.";
    } else if (newTime >= 1 && newTime <= 24) {
        FEED_INTERVAL_HOURS = newTime;
        send_time_interval(newTime); 
        alert = "SUCESSO: Intervalo de alimentacao definido para " + String(newTime) + " horas.";
    } else {
        alert = "ERRO: Valor de intervalo deve ser entre 1 e 24 horas.";
    }

    send_feedback_and_refresh(alert, token, FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: /feedQuantity (CLEAN V8.0: Usa isTokenValid())
void handleFeedQuantity() {
    String pathValue = server.arg("value");
    String token = server.arg("token");
    String alert = "";
    int newQty = pathValue.toInt();
    
    if (!isTokenValid(token)) { 
        alert = "ERRO: Token invalido ou nao fornecido.";
    } else if (newQty >= 100 && newQty <= 5000) {
        FEED_DURATION_MS = newQty;
        send_feed_duration(newQty); 
        alert = "SUCESSO: Duracao da alimentacao definida para " + String(newQty) + " ms.";
    } else {
        alert = "ERRO: Valor de duracao deve ser entre 100 e 5000 ms.";
    }
    
    send_feedback_and_refresh(alert, token, FEED_INTERVAL_HOURS, FEED_DURATION_MS);
}

// ROTA: Não Encontrada
void handleNotFound() {
    server.send(404, "text/plain", "404: Nao Encontrado.");
}