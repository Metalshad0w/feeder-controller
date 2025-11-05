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
bool isTokenValid(String token);

// ----------------------------------------------------------------------
// FUNÇÕES DE TEMPO E COMUNICAÇÃO SERIAL (REGRAS DE COMUNICAÇÃO)
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
    // Sincroniza a variável interna do ESP
    FEED_INTERVAL_HOURS = value;
    String command = "TIME=" + String(value);
    Serial.println(command);
}
void send_feed_duration(int value) {
    // Sincroniza a variável interna do ESP
    FEED_DURATION_MS = value;
    String command = "FEED_QUANTITY=" + String(value);
    Serial.println(command);
}
void send_feed_now() {
    timeClient.update();
    String command = "FEED_NOW=" + timeClient.getFormattedTime();
    Serial.println(command);
}

// ----------------------------------------------------------------------
// FUNÇÃO PRINCIPAL: SETUP (V16.0)
// ----------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    // --- Inicialização Robusta do LittleFS ---
    const int MAX_LITTLEFS_TRIES = 5;
    bool littleFS_ok = false;

    for (int i = 0; i < MAX_LITTLEFS_TRIES; i++) {
        if (LittleFS.begin()) {
            littleFS_ok = true;
            break;
        }
        delay(100);
        yield();
    }

    if (!littleFS_ok) {
        // Falha fatal.
        return;
    }

    // --- Conexão WiFi ROBUSTA e SILENCIOSA ---
    WiFi.mode(WIFI_STA); // Modo Estação (cliente)
    WiFi.begin(SSID_NAME, PASSWORD);

    int maxTries = 120;
    while (WiFi.status() != WL_CONNECTED && maxTries > 0) {
        delay(500);
        yield();
        maxTries--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        timeClient.begin();
        timeClient.update();
        // Sincroniza o tempo com o Mega na inicialização
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

    // INICIA O SERVIDOR INCONDICIONALMENTE
    server.begin();

    // Inicia o lastFeedTime para que o countdown inicie no boot
    lastFeedTime = millis();

    // Sincroniza configurações iniciais com o controlador do motor
    send_time_interval(FEED_INTERVAL_HOURS);
    send_feed_duration(FEED_DURATION_MS);
}

// ----------------------------------------------------------------------
// FUNÇÃO PRINCIPAL: LOOP (V16.0 - SEM 'send_feed_now()' AUTOMÁTICO)
// ----------------------------------------------------------------------

void loop() {
    server.handleClient();

    // 1. Manutenção da Conexão WiFi (Assíncrona e Não-Bloqueante)
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 10000) { 
            WiFi.begin(SSID_NAME, PASSWORD); 
            lastReconnectAttempt = millis();
        }
    } else {
        // 2. Sincronização de Tempo (NTP)
        updateTime(); 
        
        // 3. Lógica de Alimentação Automática: Apenas atualização do lastFeedTime para o countdown da web
        if (millis() - lastFeedTime >= (unsigned long)FEED_INTERVAL_HOURS * ONE_HOUR_MS) {
            
            // O Mega gerencia se deve alimentar ou não (janela de tempo e disparo).
            // O ESP apenas atualiza seu contador para que o website mostre o próximo ciclo correto.
            lastFeedTime = millis(); 
            
            // REMOVIDO: send_feed_now();
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

// FUNÇÃO CRUCIAL (V15.0 - LÓGICA DE EXIBIÇÃO CORRIGIDA)
String getNextFeedTimeDisplay(unsigned long lastFeedTimeMillis, int intervalHours) {
    timeClient.update();
    
    // 1. Calcula o Epoch Time do Próximo Feed Esperado
    unsigned long nextFeedMillis = lastFeedTimeMillis + (unsigned long)intervalHours * ONE_HOUR_MS;
    unsigned long nowMillis = millis();
    
    // Se o feed já passou:
    if (nextFeedMillis <= nowMillis) {
        
        int currentHour = timeClient.getHours();
        
        // Se já passou e estamos DENTRO do horário válido (Dia)
        if (currentHour >= START_HOUR && currentHour < END_HOUR) {
            return "Em 0m"; // Deveria alimentar AGORA (mas foi pulado ou está atrasado)
        } 
        
        // Se já passou e estamos FORA do horário válido (Noite)
        // O feed será reagendado para o START_HOUR mais próximo (7:00 AM).
        
        long nextValidFeedTimeSeconds = 0;
        
        // Calcular os segundos do dia atual até o alvo (7:00 AM)
        long currentSecondsOfDay = timeClient.getHours() * 3600 + timeClient.getMinutes() * 60 + timeClient.getSeconds();
        long targetSecondsOfDay = START_HOUR * 3600;
        
        // Se o horário atual (ex: 21:00) for DEPOIS do START_HOUR (7:00 AM), o alvo é amanhã.
        // O bloqueio é de END_HOUR até START_HOUR do dia seguinte.
        if (currentHour >= END_HOUR) { 
            // Tempo restante até meia-noite + Tempo da meia-noite até START_HOUR (7:00 AM)
            long secondsUntilMidnight = 24 * 3600 - currentSecondsOfDay;
            nextValidFeedTimeSeconds = secondsUntilMidnight + targetSecondsOfDay;
        } else { // Se o horário atual (ex: 3:00 AM) for ANTES do START_HOUR
            nextValidFeedTimeSeconds = targetSecondsOfDay - currentSecondsOfDay;
        }

        // Converter segundos em Horas e Minutos
        long h = nextValidFeedTimeSeconds / 3600;
        long m = (nextValidFeedTimeSeconds % 3600) / 60;

        String result = "";
        if (h > 0) result += String(h) + "h ";
        result += String(m) + "m";
        return "Em " + result; 
    }
    
    // 2. Countdown Normal
    unsigned long timeRemainingMs = nextFeedMillis - nowMillis;
    long seconds = timeRemainingMs / ONE_SECOND_MS;
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
    server.sendHeader("Cache-Control", "public, max-age=60"); // Cache de 60 segundos para o HTML
    server.sendHeader("Connection", "keep-alive");
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
        // Ação: Atualiza o tempo para reiniciar o countdown no website (Requisito 1)
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
        // A atribuição foi movida para send_time_interval
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
        // A atribuição foi movida para send_feed_duration
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