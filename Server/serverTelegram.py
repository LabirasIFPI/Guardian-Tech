from flask import Flask, request, jsonify
import requests
import os
import time

app = Flask(__name__)

# Lista global para armazenar os níveis de gás recebidos
gas_levels = []

# Obtenha o token e o chat_id das variáveis de ambiente
TELEGRAM_BOT_TOKEN = os.environ.get("TELEGRAM_BOT_TOKEN")
TELEGRAM_CHAT_ID = os.environ.get("TELEGRAM_CHAT_ID")

# Armazenar o último nível de gás notificado
last_sent_level = None

def send_telegram_notification(message):
    """
    Envia a notificação para o Telegram e retorna a resposta da API.
    """
    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    payload = {
        "chat_id": TELEGRAM_CHAT_ID,
        "text": message,
        "parse_mode": "HTML"  # Permite usar tags HTML na mensagem
    }
    for attempt in range(3):  # Tenta até 3 vezes enviar a notificação
        try:
            response = requests.post(url, json=payload)
            if response.status_code == 200:
                return response.json()  # Retorna a resposta da API do Telegram
            print(f"Tentativa {attempt + 1} falhou com código: {response.status_code}")
        except Exception as e:
            print(f"Erro ao enviar mensagem para o Telegram na tentativa {attempt + 1}: {e}")
        time.sleep(2)  # Aguarda 2 segundos antes de tentar novamente
    return {"error": "Falha em todas as tentativas"}

@app.route('/')
def index():
    return "Servidor rodando! Use o endpoint /notify_gas_level para enviar e consultar níveis de gás."

@app.route('/notify_gas_level', methods=['GET', 'POST'])
def notify_gas_level():
    global last_sent_level

    if request.method == 'POST':
        data = request.get_json()
        if not data or 'gas_level' not in data:
            return jsonify({"error": "No gas level provided"}), 400

        gas_level = data['gas_level']

        # Ignorar níveis abaixo de 1500
        if gas_level < 1500:
            return jsonify({
                "status": "ignored",
                "message": "Gas level below threshold, no action taken."
            }), 200

        # Evitar notificações repetidas
        if gas_level == last_sent_level:
            return jsonify({
                "status": "ignored",
                "message": "Duplicate gas level ignored."
            }), 200

        gas_levels.append(gas_level)
        last_sent_level = gas_level  # Atualiza o último nível enviado

        # Monta a mensagem com emojis e formatação HTML sofisticada
        if 1500 <= gas_level <= 2000:
            message = (
                "⚠️ <b>Aviso: Nível de Gás Moderado</b>\n"
                f"Valor medido: <b>{gas_level}</b> ppm.\n"
                "Recomenda-se monitoramento intensificado e análise dos equipamentos. 🔍"
            )
        elif gas_level > 2000:
            message = (
                "🚨 <b>ALERTA CRÍTICO</b> 🚨\n"
                f"Valor medido: <b>{gas_level}</b> ppm.\n"
                "AÇÃO IMEDIATA NECESSÁRIA! Verifique os equipamentos e tome medidas de segurança. 🛑\n"
                "Sua segurança é prioridade! 💥"
            )

        # Envia a mensagem para o Telegram
        telegram_response = send_telegram_notification(message)
        
        print("Mensagem enviada ao Telegram:", message)
        print("Resposta do Telegram:", telegram_response)

        return jsonify({
            "status": "success",
            "gas_level": gas_level,
            "telegram_response": telegram_response
        }), 200

    elif request.method == 'GET':
        # Retorna o histórico de níveis de gás
        return jsonify({"gas_levels": gas_levels, "count": len(gas_levels)}), 200

if __name__ == '__main__':
    PORT = int(os.environ.get("PORT", 5000))
    app.run(host="0.0.0.0", port=PORT)