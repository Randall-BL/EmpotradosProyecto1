/*
 * Proyecto I - Robot Aspiradora Autonomo
 * CE-1113 Sistemas Empotrados, Instituto Tecnologico de Costa Rica
 *
 * Archivo derivado del proyecto Proyecto1_RobotYocto---Sistemas-Empotrados,
 * MIT License - Copyright (c) 2026 Javier Tenorio Cervantes. Ver NOTICE.md.
 */

#ifndef ROBOT_HARDWARE_H
#define ROBOT_HARDWARE_H

#include "lib_motors.h"
#include "lib_sensors.h"

extern SensorUltrasonico sensorFrontal;
extern SensorUltrasonico sensorLateralIzq;
extern SensorUltrasonico sensorLateralDer;

int robot_hw_init();
int robot_hw_get_pi();    // ← nuevo — expone el handle
void robot_hw_cleanup();
double robot_get_distancia_frontal();
double robot_get_distancia_izq();
double robot_get_distancia_der();

#endif // ROBOT_HARDWARE_H
