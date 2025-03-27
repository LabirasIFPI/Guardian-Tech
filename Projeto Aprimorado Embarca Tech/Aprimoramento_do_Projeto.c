#include <stdio.h> // Inclui a biblioteca padrão de entrada e saída
#include <stdint.h> // Inclui a biblioteca de inteiros de tamanho fixo
#include <string.h> // Inclui a biblioteca de manipulação de string
#include <math.h> // Inclui a biblioteca matemática
#include "pico/stdlib.h" // Biblioteca padrão do Raspberry Pi Pico
#include "ssd1306.h" // Biblioteca para controle do display OLED SSD1306
#include "hardware/i2c.h" // Biblioteca para comunicação via I2C
#include "hardware/adc.h" // Biblioteca para uso do conversor analógico-digital (ADC)
#include "hardware/pwm.h" // Biblioteca para controle de PWM
#include "ws2812b_animation.h" // Biblioteca para controle de LEDs WS2812B
#include "hardware/gpio.h" // Biblioteca para controle de GPIOs
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/dns.h"

 #define WIFI_SSID "Cabecita"
 #define WIFI_PASSWORD "Claritazita1"
 #define API_HOST "shinkansen.proxy.rlwy.net"
 #define API_PORT 38884
 #define API_PATH "/notify_gas_level"
 
 // Definições para o display
 #define SCREEN_WIDTH 128 // Largura do display OLED
 #define SCREEN_HEIGHT 64 // Altura do display OLED
 #define SCREEN_ADDRESS 0x3C // Endereço I2C do display OLED
 #define I2C_SDA 14 // Pino SDA do barramento I2C
 #define I2C_SCL 15 // Pino SCL do barramento I2C
 
 // Definições para componentes
 #define GAS_SENSOR_PIN 28 // Pino do sensor de gás (entrada analógica)
 #define BUZZER_A_PIN 21 // Pino do Buzzer A
 #define BUZZER_B_PIN 23 // Pino do Buzzer B
 
 #define NUM_LEDS 25 // Quantidade total de LEDs na matriz
 #define LED_PIN 7 // Pino dos LEDs WS2812B
 #define DHT_PIN 8 // Pino do Sensor DHT11/DHT22
 
 #define LEVEL_LOW 500 // Nível baixo de gás
 #define LEVEL_LOW_HIGH 1000 // Nível intermediário baixo de gás
 #define LEVEL_MEDIUM_LOW 1500 // Nível médio baixo de gás
 #define LEVEL_MEDIUM 2000 // Nível médio de gás
 
 // Variáveis globais
 uint slice_num_a; // Numero da slice PWM para o buzzer A
 uint slice_num_b; // Numero da slice PWM para o buzzer B
 float calibration_factor = 1.0; // Fator de calibração do sensor de gás
 ssd1306_t display; // Variável para o display SSD1306
 char nomeSite[60];

 // Função para exibir texto no display
void demotxt(const char *texto) {
    ssd1306_clear(&display); // Limpa o display
    ssd1306_draw_string(&display, 0, 0, 1, texto); // Desenha a string no display
    ssd1306_show(&display); // Atualiza o display para mostrar o conteúdo
    sleep_ms(1000); // Aguarda 1 segundo
}

// Função para leitura do sensor DHT11
void dht11_read(int *temperature, int *humidity) {
    uint8_t bits[5] = {0}; // Array para armazenar os dados do sensor
    uint32_t start_time; // Variável para armazenar o tempo inicial

    // Configura o pino do DHT como saída e envia pulso de inicialização
    gpio_set_dir(DHT_PIN, GPIO_OUT); // Define o pino como saída
    gpio_put(DHT_PIN, 0); // Define o pino como LOW
    sleep_ms(18); // Aguarda pelo tempo necessário para inicialização
    gpio_put(DHT_PIN, 1); // Define o pino como HIGH
    sleep_us(40); // Aguarda por 40 microsegundos
    gpio_set_dir(DHT_PIN, GPIO_IN); // Define o pino como entrada

    // Aguarda a resposta do sensor com timeout
    start_time = to_ms_since_boot(get_absolute_time()); // Marca o início do tempo
    while (gpio_get(DHT_PIN) == 1) { // Aguarda resposta do sensor
        if (to_ms_since_boot(get_absolute_time()) - start_time > 100) { // Timeout
            printf("Erro: Timeout esperando resposta do DHT11\n");
            *temperature = -1; // Define temperatura como erro
            *humidity = -1; // Define umidade como erro
            return;
        }
    }

    // Aguarda pulso baixo do sensor
    start_time = to_ms_since_boot(get_absolute_time()); 
    while (gpio_get(DHT_PIN) == 0) { 
        if (to_ms_since_boot(get_absolute_time()) - start_time > 100) { // Timeout
            printf("Erro: Timeout esperando pulso baixo\n");
            *temperature = -1;
            *humidity = -1;
            return;
        }
    }

    // Aguarda pulso alto do sensor
    start_time = to_ms_since_boot(get_absolute_time()); 
    while (gpio_get(DHT_PIN) == 1) { 
        if (to_ms_since_boot(get_absolute_time()) - start_time > 100) { // Timeout
            printf("Erro: Timeout esperando pulso alto\n");
            *temperature = -1;
            *humidity = -1;
            return;
        }
    }

    // Leitura dos 40 bits de dados
    for (int i = 0; i < 40; i++) {
        // Aguarda início do bit
        start_time = to_ms_since_boot(get_absolute_time()); 
        while (gpio_get(DHT_PIN) == 0) { 
            if (to_ms_since_boot(get_absolute_time()) - start_time > 100) { // Timeout
                printf("Erro: Timeout esperando bit %d\n", i);
                *temperature = -1;
                *humidity = -1;
                return;
            }
        }

        // Aguarda tempo necessário e lê o bit
        sleep_us(30); // Aguarda 30 microsegundos
        if (gpio_get(DHT_PIN) == 1) { // Se ainda estiver alto, define o bit como 1
            bits[i / 8] |= (1 << (7 - (i % 8))); 
        }

        // Aguarda final do bit
        start_time = to_ms_since_boot(get_absolute_time()); 
        while (gpio_get(DHT_PIN) == 1) { 
            if (to_ms_since_boot(get_absolute_time()) - start_time > 100) { // Timeout
                printf("Erro: Timeout no pulso baixo do bit %d\n", i);
                *temperature = -1;
                *humidity = -1;
                return;
            }
        }
    }

    // Verifica checksum
    if ((bits[0] + bits[1] + bits[2] + bits[3]) == bits[4]) { // Se checksum é válido
        *humidity = bits[0]; // Define umidade
        *temperature = bits[2]; // Define temperatura
    } else { // Caso contrário, retorna erro
        printf("Erro: Checksum inválido\n");
        *humidity = -1;
        *temperature = -1;
    }
}
 
 void buzzer_init() {
    //Inicialização do buzzer
    gpio_set_function(BUZZER_A_PIN, GPIO_FUNC_PWM);
    slice_num_a = pwm_gpio_to_slice_num(BUZZER_A_PIN);
    pwm_set_wrap(slice_num_a, 25000);
    pwm_set_gpio_level(BUZZER_A_PIN, 12500);
    pwm_set_enabled(slice_num_a, false);

    gpio_set_function(BUZZER_B_PIN, GPIO_FUNC_PWM);
    slice_num_b = pwm_gpio_to_slice_num(BUZZER_B_PIN);
    pwm_set_wrap(slice_num_b, 25000);
    pwm_set_gpio_level(BUZZER_B_PIN, 12500);
    pwm_set_enabled(slice_num_b, false);
}
 
 // Função para desenhar uma onda no display
 void draw_wave(int amplitude, int frequency, int phase, int offset, int y) {
    // Desenha ondas senoidais para embelezar o display
     for (int x = 0; x < SCREEN_WIDTH; x++) {
         int wave_y = y + amplitude * sin((2 * M_PI * frequency * (x + phase)) / SCREEN_WIDTH);
         ssd1306_draw_pixel(&display, x, wave_y + offset);
     }
 }

 typedef struct {
    struct tcp_pcb *pcb;
    bool complete;
    char request[512];
    char response[512];
    size_t response_len;
    ip_addr_t remote_addr;
} HTTP_CLIENT_T;

// Função chamada quando há dados recebidos no cliente TCP
static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    HTTP_CLIENT_T *state = (HTTP_CLIENT_T *)arg; // Converte o argumento para o tipo HTTP_CLIENT_T
    if (!p) { // Verifica se a estrutura `pbuf` é nula (conexão finalizada)
        state->complete = true; // Marca a requisição como completa
        return ERR_OK; // Retorna com sucesso
    }
    
    if (p->tot_len > 0) { // Verifica se há dados recebidos
        // Determina o número de bytes a copiar, respeitando o limite do buffer
        size_t to_copy = (p->tot_len < sizeof(state->response) - state->response_len - 1) 
        ? p->tot_len : sizeof(state->response) - state->response_len - 1;
        // Copia os dados da `pbuf` para o buffer de resposta
        pbuf_copy_partial(p, state->response + state->response_len, to_copy, 0);
        state->response_len += to_copy; // Atualiza o tamanho da resposta
        state->response[state->response_len] = '\0'; // Finaliza a string com null terminator
    }
    
    tcp_recved(tpcb, p->tot_len); // Indica ao TCP que os dados foram recebidos
    pbuf_free(p); // Libera a `pbuf` após o processamento
    return ERR_OK; // Retorna com sucesso
}

// Função chamada quando a conexão TCP é estabelecida
static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    if (err != ERR_OK) { // Verifica se houve erro na conexão
        printf("Erro ao conectar: %d\n", err);
        return err; // Retorna o erro
    }

    HTTP_CLIENT_T *state = (HTTP_CLIENT_T *)arg; // Converte o argumento para o tipo HTTP_CLIENT_T
    // Envia a requisição HTTP para o servidor
    err = tcp_write(tpcb, state->request, strlen(state->request), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) { // Verifica se houve erro ao enviar a requisição
        printf("Erro ao enviar requisição: %d\n", err);
        return err; // Retorna o erro
    }
    
    err = tcp_output(tpcb); // Força o envio dos dados através do socket TCP
    return err; // Retorna o status da operação
}

// Função para enviar o nível de gás para um servidor através de API HTTP
bool send_gas_level(int gas_level) {
    HTTP_CLIENT_T *state = (HTTP_CLIENT_T *)calloc(1, sizeof(HTTP_CLIENT_T)); // Aloca memória para o estado do cliente HTTP
    if (!state) {
        printf("Erro ao alocar memória\n");
        return false;
    }

    char json_data[128];
    snprintf(json_data, sizeof(json_data), "{\"gas_level\": %d}", gas_level); // Formata os dados JSON com o nível de gás

    snprintf(state->request, sizeof(state->request),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             API_PATH, API_HOST, (int)strlen(json_data), json_data);

    // Corrigido: Formatação da URL com snprintf
    char nomeSite[256];
    snprintf(nomeSite, sizeof(nomeSite), "https://%s/%s", API_HOST, API_PATH);

    printf("Enviando JSON para API...\n%s\n", state->request);

    // Resolve o nome do domínio para um endereço IP usando DNS
    err_t err = dns_gethostbyname(API_HOST, &state->remote_addr, NULL, NULL);
    if (err == ERR_INPROGRESS) {
        printf("Resolução de DNS em progresso...\n");
        // Se a resolução de DNS for assíncrona, seria necessário implementar um callback
        free(state);
        return false; // Modifique essa lógica para lidar com callbacks se necessário
    } else if (err != ERR_OK) {
        printf("Erro ao resolver DNS: %d\n", err);
        free(state);
        return false;
    }

    state->pcb = tcp_new();
    if (!state->pcb) {
        printf("Erro ao criar PCB TCP\n");
        free(state);
        return false;
    }

    tcp_arg(state->pcb, state);
    tcp_recv(state->pcb, tcp_client_recv);

    // Conectando ao servidor usando o IP resolvido
    err = tcp_connect(state->pcb, &state->remote_addr, API_PORT, tcp_client_connected);
    if (err != ERR_OK) {
        printf("Erro ao conectar ao servidor: %d\n", err);
        free(state);
        return false;
    }

    // Aguarde um tempo para garantir que a conexão seja estabelecida antes de enviar o POST
    sleep_ms(7000);

    // Envia os dados para a API
    printf("Resposta da API: %s\n", state->response);
    
    // Libere os recursos alocados
    free(state);
    tcp_close(state->pcb);
    return true;
}

 
 // Função de callback para receber dados TCP
 err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
     if (p != NULL) {
         // Copiar os dados para um buffer legível
         char buffer[256];
         memset(buffer, 0, sizeof(buffer));
         strncpy(buffer, (char*)p->payload, p->len);
         buffer[p->len] = '\0'; // Garantir terminação nula
 
         printf("Recebido: %s\n", buffer);
 
         // Liberar buffer
         pbuf_free(p);
     }
     return ERR_OK;
 }
 
 // Função de callback para gerenciar nova conexão
 err_t tcp_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
     // Definir a função de recebimento para esta nova conexão
     tcp_recv(newpcb, tcp_recv_callback);
     return ERR_OK;
 }
 
 // Função para iniciar o servidor TCP
 void tcp_server(void) {
     struct tcp_pcb *pcb;
     err_t err;
 
     printf("Iniciando servidor TCP...\n");
 
     // Criar um novo PCB (control block) para o servidor TCP
     pcb = tcp_new();
     if (pcb == NULL) {
         printf("Erro ao criar o PCB TCP.\n");
         return;
     }
 
     // Vincular o servidor ao endereço e porta desejada
     ip_addr_t ipaddr;
     IP4_ADDR(&ipaddr, 0, 0, 0, 0);  // Ou use IP_ADDR_ANY para todas as interfaces
     err = tcp_bind(pcb, &ipaddr, API_PORT);
     if (err != ERR_OK) {
         printf("Erro ao vincular ao endereço e porta.\n");
         return;
     }
 
     // Colocar o servidor para ouvir conexões
     pcb = tcp_listen(pcb);
     if (pcb == NULL) {
         printf("Erro ao colocar o servidor em escuta.\n");
         return;
     }
 
     // Configurar a função de aceitação das conexões
     tcp_accept(pcb, tcp_accept_callback);
     printf("Servidor TCP iniciado na porta %d.\n", API_PORT);
 }
 
 void init_system() {
    // Inicializa UART para depuração
    stdio_init_all();
    ws2812b_set_global_dimming(7);
    gpio_init(DHT_PIN);
    buzzer_init();

    // Inicializa I2C
    i2c_init(i2c1, 400 * 1000); // 400 kHz
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicializa o display OLED
    if (!ssd1306_init(&display, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_ADDRESS, i2c1)) {
        printf("Falha ao inicializar o display SSD1306\n");
        return;
    }

    // Configura ADC para o sensor de gás
    adc_init();
    adc_gpio_init(GAS_SENSOR_PIN);

    // Inicializa LEDs
    ws2812b_init(pio0, LED_PIN, NUM_LEDS);

    // Configura Wi-Fi
    cyw43_arch_init();
    cyw43_arch_enable_sta_mode();
}

void connect_wifi() {
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        demotxt("Falha ao conectar ao WiFi.");
        sleep_ms(3000);
    } else {
        ssd1306_clear(&display);

        // Exibe "Conectado ao WiFi"
        int x_centered = (SCREEN_WIDTH - (strlen("Conectado ao WiFi") * 6)) / 2;
        ssd1306_draw_string(&display, x_centered, 16, 1, "Conectado ao WiFi");

        // Exibe nome da rede WiFi e endereço IP
        ip4_addr_t ip = cyw43_state.netif[0].ip_addr;
        uint32_t ip_raw = ip4_addr_get_u32(&ip);
        uint8_t ip_address[4] = {
            (uint8_t)(ip_raw & 0xFF),
            (uint8_t)((ip_raw >> 8) & 0xFF),
            (uint8_t)((ip_raw >> 16) & 0xFF),
            (uint8_t)((ip_raw >> 24) & 0xFF)
        };
        char bufferWifi[50];
        sprintf(bufferWifi, "IP: %d.%d.%d.%d", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
        x_centered = (SCREEN_WIDTH - (strlen(bufferWifi) * 6)) / 2;
        ssd1306_draw_string(&display, x_centered, 32, 1, bufferWifi);

        ssd1306_show(&display);
        sleep_ms(5000);
    }
}

 // Função para configurar a cor dos LEDs com base no nível de gás e acionar os buzzers
 void set_leds_and_buzzers(uint16_t gas_level) {
    if (gas_level <= LEVEL_LOW) {
        // Verde nas duas primeiras fileiras e buzzers desligados
        ws2812b_fill_all(GRB_BLACK);
        ws2812b_fill(0, 4, GRB_GREEN); // LEDs de 0 a 4
        pwm_set_enabled(slice_num_a, false); // Desativa o Buzzer A
        pwm_set_enabled(slice_num_b, false); // Desativa o Buzzer B
    } else if (gas_level <= LEVEL_LOW_HIGH) {
        ws2812b_fill_all(GRB_BLACK);
        ws2812b_fill(0, 9, GRB_GREEN); // LEDs de 0 a 9
        pwm_set_enabled(slice_num_a, false); // Desativa o Buzzer A
        pwm_set_enabled(slice_num_b, false); // Desativa o Buzzer B
    } else if (gas_level <= LEVEL_MEDIUM_LOW) {
        // Amarelo nas três primeiras fileiras e buzzers desligados
        ws2812b_fill_all(GRB_BLACK);
        ws2812b_fill(0, 14, GRB_YELLOW); // LEDs de 0 a 14
        pwm_set_enabled(slice_num_a, false); // Desativa o Buzzer A
        pwm_set_enabled(slice_num_b, false); // Desativa o Buzzer B
    } else if (gas_level <= LEVEL_MEDIUM) {
        // Amarelo nas quatro primeiras fileiras e buzzers desligados
        ws2812b_fill_all(GRB_BLACK);
        ws2812b_fill(0, 19, GRB_YELLOW); // LEDs de 0 a 19
        pwm_set_enabled(slice_num_a, false); // Desativa o Buzzer A
        pwm_set_enabled(slice_num_b, false); // Desativa o Buzzer B
        send_gas_level(gas_level);
        sleep_ms(2000);

    } else {
        // Vermelho em todas as fileiras e buzzers ligados
        ws2812b_fill_all(GRB_BLACK);
        ws2812b_fill_all(GRB_RED); // Todos os LEDs
        //  pwm_set_enabled(slice_num_a, true); // Ativa o Buzzer A
        //  pwm_set_enabled(slice_num_b, true); // Ativa o Buzzer B
        send_gas_level(gas_level);
        sleep_ms(2000);
    }
    ws2812b_render();
}

void main_loop() {
    int phase = 0;

    while (true) {
        // Lê o sensor de gás
        adc_select_input(2);
        uint16_t gas_level = adc_read();
        gas_level = (uint16_t)(gas_level * (2.0 - calibration_factor));
        gas_level = gas_level > 4095 ? 4095 : gas_level;
        uint16_t gas_percentage = (gas_level / 4095.0) * 100;

        // Lê o sensor DHT11
        int temperature, humidity;
        dht11_read(&temperature, &humidity);
        if (temperature != -1 && humidity != -1) {
            calibration_factor = 1.0 - (0.01 * (temperature - 25));
        }

        // Atualiza display
        ssd1306_clear(&display);
        char buffer[32];
        sprintf(buffer, "Gas: %d%%", gas_percentage);
        int x_centered = (SCREEN_WIDTH - (strlen(buffer) * 6)) / 2;
        ssd1306_draw_string(&display, x_centered, 24, 1, buffer);

        sprintf(buffer, "Temp: %dC", temperature);
        ssd1306_draw_string(&display, x_centered, 40, 1, buffer);
        ssd1306_draw_string(&display, x_centered, 45, 1, nomeSite);
        ssd1306_show(&display);

        // Atualiza LEDs e buzzers
        set_leds_and_buzzers(gas_level);
        
        
        // Pequeno atraso para evitar sobrecarga
        sleep_ms(500);
    }
}

int main() {
    init_system();
    connect_wifi();
    tcp_server(); // Inicia servidor TCP
    main_loop(); // Loop principal
    return 0;
} 