/*! @mainpage Sistema de Climantización en una UTI
 *
 * @section genDesc General Description
 *
 * Este programa controla un sistema de temperatura, humedad y luz en un entorno UTI
 * activando el aire acondicionado, calefacción el motor de cortina según umbrales 
 * configurados.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * | Peripheral  | ESP32        |
 * |-------------|--------------|
 * | Sensor Temp | GPIO_CH1     |
 * | Sensor Hum  | GPIO_CH1     |
 * | Sensor Luz  | GPIO_CH2     |
 * | Servomotor  | GPIO_18     |
 *
 * @section changelog Changelog
 *
 * |   Date     | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 04/10/2024 | Creación del documento                         |
 * | 12/11/2024 | Pruebas finales                                |
 * | 13/11/2024 | Documentación final                            |
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "analog_io_mcu.h"
#include "uart_mcu.h"
#include "timer_mcu.h"
#include "gpio_mcu.h"
#include "ble_mcu.h"
#include "led.h"
#include "string.h"
#include "rtc_mcu.h"
#include "dht11.h"
#include "servo_sg90.h"

/*==================[macros and definitions]=================================*/
#define CONFIG_PERIOD_S_A 1000000  // Periodo de 1 segundos
#define CONFIG_PERIOD_S_B 2000000  // Periodo de 2 segundos

/// Inicialización del Identificador de tareas
TaskHandle_t climate_control_task_handle = NULL;
TaskHandle_t light_curtain_control_task_handle = NULL;
// Task creation for climate control based on temperature and humidity

/*==================[internal data definition]===============================*/

/// Variable global para encendido/apagado del sistema
bool system_on = false;


char mensaje_cortina[20];
char mensaje_humedad[20];
char mensaje_lum[20];
char mensaje_temp[20];
char mensaje_cale[20];
char mensaje_aire[20];
char fecha_hora[30];

typedef struct {
	gpio_t pin; ///< Pin GPIO a configurar
	io_t dir;   ///< Dirección del pin: entrada o salida
} gpioConfig_t;

/*==================[internal functions declaration]=========================*/

/**
 * @brief Envía la temperatura por Bluetooth en el formato *Txx*
 * @param temperatura La temperatura medida en grados Celsius.
 */
 static void sendTemperatureViaBluetooth(uint16_t temperatura, bool estado_aire_acondicionado, bool estado_calefaccion) {
    strcpy(mensaje_temp, "");
    sprintf(mensaje_temp, "*T%u*", temperatura); // Formato *Txx*
    BleSendString(mensaje_temp);

    if (estado_aire_acondicionado) {
        strcpy(mensaje_aire, "");
        sprintf(mensaje_aire, "*S ENCENDIDO\n*"); // Formato *M*
        //sprintf(mensaje_aire, "*S%u*", temperatura);
        BleSendString(mensaje_aire);
    }
    else {
        strcpy(mensaje_aire, "");
        sprintf(mensaje_aire, "*S APAGADO\n*"); // Formato *M*
        //sprintf(mensaje_aire, "*S%u*", temperatura);
        BleSendString(mensaje_aire);
    } 

    if (estado_calefaccion) {
        strcpy(mensaje_cale, "");
        sprintf(mensaje_cale, "*C ENCENDIDA\n*"); // Formato *M*
        BleSendString(mensaje_cale);
    }
    else {
        strcpy(mensaje_cale, "");
        sprintf(mensaje_cale, "*C APAGADA\n*"); // Formato *M*
         BleSendString(mensaje_cale);
    } 
}

static void sendHumidityViaBluetooth(uint16_t humedad) {
    strcpy(mensaje_humedad, "");
    sprintf(mensaje_humedad, "*H%u%%*", humedad);
    BleSendString(mensaje_humedad);
    strcpy(mensaje_humedad, "");
    sprintf(mensaje_humedad, "*I%u*", humedad);
    BleSendString(mensaje_humedad);
}
/**
 * @brief Envía la temperatura por Bluetooth en el formato *Txx*
 * @param temperatura La temperatura medida en grados Celsius.
 */
static void sendLightLevelOverBluetooth(uint16_t luminosidad, bool estado_motor) {
    strcpy(mensaje_lum, "");
    sprintf(mensaje_lum, "*L %u\n*", luminosidad); // Formato *Txx*
    BleSendString(mensaje_lum);
    if (estado_motor) {
        strcpy(mensaje_cortina, "");
        sprintf(mensaje_cortina, "*M ABIERTA\n*"); // Formato *M*
        BleSendString(mensaje_cortina);
    }
    else {
        strcpy(mensaje_cortina, "");
        sprintf(mensaje_cortina, "*M CERRADA\n*"); // Formato *M*
        BleSendString(mensaje_cortina);
    }   
}

/**
 * @brief Tarea que controla el aire acondicionado y la calefacción según la temperatura
 * @param pParam Parámetro de la tarea (no utilizado).
 */
static void climateControlTask(void *pParam) {
    
    // Definición de los umbrales de temperatura en grados Celsius como constantes.
    const uint16_t HIGH_TEMP_THRESHOLD = 24;   // Umbral alto para encender el aire acondicionado
    const uint16_t LOW_TEMP_THRESHOLD = 21;    // Umbral bajo para apagar el aire acondicionado
    const uint16_t HEAT_THRESHOLD = 18;        // Umbral bajo para encender la calefacción
    bool air_state = false;              // Estado del aire acondicionado (false = apagado)
    bool heat_state = false;             // Estado de la calefacción (false = apagado)

     while (true) 
     {
        // Espera notificación del temporizador para ejecutar la tarea a intervalos definidos.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Espera notificación del timer
        // Verifica si el sistema está encendido antes de continuar con el control climático.
        if (system_on) { 
            float humidity = 0;  // Variable para almacenar la humedad
            float temperature = 0; // Variable para almacenar la temperatura
            // Llama a la función para leer datos del sensor DHT11 (temperatura y humedad).
            if (dht11Read(&humidity, &temperature)) {
                // Envía el dato de la humedad por Bluetooth.
                sendHumidityViaBluetooth(humidity);

                // Verifica la temperatura y ejecuta las acciones correspondientes
                // para encender o apagar el aire acondicionado o la calefacción.
                
                if (temperature > HIGH_TEMP_THRESHOLD) {  // temperatura mayor a 24 grados
                    // Encender aire acondicionado si la temperatura es mayor que 24°C
                    printf("Temperatura alta: Encendiendo aire acondicionado\n");
                    printf("Temperatura: %.2f\n", temperature);
                    air_state = true;    // Enciende el aire acondicionado
                    heat_state = false;  // Apaga la calefacción
                    sendTemperatureViaBluetooth(temperature, air_state, heat_state); 
                    LedOn(LED_2);  // Enciende LED 2 para simular aire acondicionado
                    LedOff(LED_3); // Apaga LED 3 para simular calefacción

                } else if (temperature < HEAT_THRESHOLD) {
                    // Encender calefacción si la temperatura es menor que 18°C
                    printf("Temperatura baja: Encendiendo calefacción\n");
                    printf("Temperatura: %.2f\n", temperature);
                    air_state = false;  // Apaga el aire acondicionado
                    heat_state = true;  // Enciende la calefacción
                    sendTemperatureViaBluetooth(temperature, air_state, heat_state);
                    LedOn(LED_3);  // Enciende LED 3 para simular calefacción
                    LedOff(LED_2); // Apaga LED 2 para simular aire acondicionado

                } else if (temperature <= LOW_TEMP_THRESHOLD) {
                    // Apagar aire acondicionado si la temperatura baja a 21°C o menos
                    printf("Temperatura moderada: Apagando aire acondicionado\n");
                    printf("Temperatura: %.2f\n", temperature);
                    air_state = false;  // Apaga el aire acondicionado
                    heat_state = false; // Apaga la calefacción
                    sendTemperatureViaBluetooth(temperature, air_state, heat_state);
                    LedOff(LED_2); // Apaga LED 2 para simular aire acondicionado
                    LedOff(LED_3); // Apaga LED 3 para simular calefacción
                }
            }
        }
        else{
            sendHumidityViaBluetooth(0);
            sendTemperatureViaBluetooth(0, false, false);
        }

    }
}

/**
 * @brief Tarea que controla la luminosidad y acciona el motor de cortina
 * @param pParam Parámetro pasado a la tarea (no utilizado en este caso).
 */
static void lightCurtainControlTask(void *pParam) {
    
   // Arreglo para almacenar las 10 muestras de luz.
    uint16_t light_samples[10]; 

    // Umbral de luz para activar o desactivar la cortina.
    const uint16_t LIGHT_THRESHOLD = 600;  

    // Estado del motor de la cortina (abierta o cerrada).
    bool curtain_motor_state = false;             

     while (true) {
        // Realiza la espera de la notificación que indicará la activación de la tarea
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  

        // Verifica si el sistema está encendido antes de proceder.
        if (system_on) {  
                
            // Realiza 10 lecturas del sensor de luz.
            for (int i = 0; i < 10; i++) {
                AnalogInputReadSingle(CH2, &light_samples[i]); // Lee un valor del sensor de luz
            }

            // Calcula el promedio de las muestras de luz.
            uint32_t light_sum = 0; 
            for (int i = 0; i < 10; i++) {
                light_sum += light_samples[i];  // Suma las lecturas de luz
            }
            uint16_t avg_light = light_sum / 10;  // Promedio de las 10 muestras de luz.

            // Convierte el valor promedio de luz a Lux, dependiendo de la calibración del sensor.
            uint16_t avg_light_lux = avg_light / 5;  
            int8_t angle_apertura = 0;
            int8_t angle_cierre = -90;
                    // Controla el motor de la cortina según el umbral de luz
            if (avg_light_lux > LIGHT_THRESHOLD) {
                // Si la luminosidad es alta, activa el motor de la cortina.
                printf("Alta luminosidad: Activando motor de cortina\n");
                printf("Luminosidad: %d Lux\n", avg_light_lux);
                ServoMove(SERVO_0, angle_apertura);
                curtain_motor_state = true;  // Cambia el estado del motor a activado.
                sendLightLevelOverBluetooth(avg_light_lux, curtain_motor_state);  // Envía el estado por Bluetooth.
                
            } else {
                // Si la luminosidad es baja, desactiva el motor de la cortina.
                printf("Luminosidad baja: Cortina desactivada\n");
                printf("Luminosidad: %d Lux\n", avg_light_lux);
                ServoMove(SERVO_0, angle_cierre);
                curtain_motor_state = false;  // Cambia el estado del motor a desactivado.
                sendLightLevelOverBluetooth(avg_light_lux, curtain_motor_state);  // Envía el estado por Bluetooth.
                
            }
        }
        else{
            sendLightLevelOverBluetooth(0, false);
        }
    }
}


/**
 * @brief Función que lee los datos recibidos por Bluetooth para activar/desactivar el sistema
 * @param data Puntero a los datos recibidos por Bluetooth.
 * @param length Longitud de los datos recibidos.
 */
static void readBleData(uint8_t *data, uint8_t length) {
    // Verifica si el primer byte de los datos recibidos es 'E', lo que indica que se debe encender el sistema.
    if (data[0] == 'E') {
        system_on = true;  // Enciende el sistema.
        printf("Sistema encendido\n");
        LedOn(LED_1); // Enciende el LED 1 para indicar que el sistema está activo.
    } 
    // Verifica si el primer byte de los datos recibidos es 'A', lo que indica que se debe apagar el sistema.
    else if (data[0] == 'A') {
        system_on = false;  // Apaga el sistema.
        printf("Sistema apagado\n");
        LedOff(LED_1); // Apaga el LED 1 para indicar que el sistema está inactivo.
    }
}

/**
 * @brief Función de callback para el Timer A
 */
void funcionTimerA(void *pParam) {
    vTaskNotifyGiveFromISR(climate_control_task_handle, pdFALSE);  // Notificación a la tarea de medicion de temperatura y humedad
}

/**
 * @brief Función de callback para el Timer B
 */
void funcionTimerB(void *pParam) {
    vTaskNotifyGiveFromISR(light_curtain_control_task_handle, pdFALSE);  // Notificación a la tarea de conversión ADC obtener medición de luz
}

/*==================[external functions definition]==========================*/
void app_main(void){
    
    // Inicializa los LEDs para
    // 1: Sistema encendido
    // 2: Aire acondicionado encendido
    // 3: Motor cortina encendido

    LedsInit();  
    
    /// Configuración de Bluetooth
    ble_config_t ble_configuration = {
        "Sistema_UTI2",
        readBleData
    };

    BleInit(&ble_configuration);

    dht11Init(GPIO_1);

    /*gpioConfig_t vectorGpio[1]= {
		{GPIO_21, GPIO_OUTPUT},  //
	};

    GPIOInit(vectorGpio[0].pin, vectorGpio[0].dir);

    GPIOOn(GPIO_21);*/

    ServoInit(SERVO_0, GPIO_18);
    ServoMove(SERVO_0, -90); 
    printf("Cortina apagada\n");

    // Configuración de entrada del sensor de luz
    analog_input_config_t Analog_config_light = {
        .input = CH2,
        .mode = ADC_SINGLE
    };
    AnalogInputInit(&Analog_config_light);
    
    // Configuración e inicialización del timerA de 1 segundo control clima
    timer_config_t timerA = {
        .timer = TIMER_A,
        .period = CONFIG_PERIOD_S_A,
        .func_p = funcionTimerA,
        .param_p = NULL
    };
    TimerInit(&timerA);

    // Configuración e inicialización del timerB de 2 segundo control de luz
    timer_config_t timerB = {
        .timer = TIMER_B,
        .period = CONFIG_PERIOD_S_B,
        .func_p = funcionTimerB,
        .param_p = NULL
    };
    TimerInit(&timerB);


    // Task creation for climate control based on temperature and humidity
    xTaskCreate(&climateControlTask, "climateControlTask", 2048, NULL, 4, &climate_control_task_handle);

    // Task creation for controlling curtain based on ambient light
    xTaskCreate(&lightCurtainControlTask, "lightCurtainControlTask", 2048, NULL, 4, &light_curtain_control_task_handle);

    TimerStart(timerA.timer);
    TimerStart(timerB.timer);
    
}
/*==================[end of file]============================================*/