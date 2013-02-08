/*
 * Copyright 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Copyright (C) 2013 Freescale Semiconductor, Inc. */

/******************************************************************************
 *
 *  Filename:      upio.h
 *
 *  Description:   Contains definitions used for I/O controls
 *
 ******************************************************************************/

#ifndef UPIO_H
#define UPIO_H

/******************************************************************************
**  Constants & Macros
******************************************************************************/

#define UPIO_BT_POWER_OFF 0
#define UPIO_BT_POWER_ON  1

/******************************************************************************
**  Extern variables and functions
******************************************************************************/

/******************************************************************************
**  Functions
******************************************************************************/

/*******************************************************************************
**
** Function        upio_set_bluetooth_power
**
** Description     Interact with low layer driver to set Bluetooth power
**                 on/off.
**
** Returns         0  : SUCCESS or Not-Applicable
**                 <0 : ERROR
**
*******************************************************************************/
int upio_set_bluetooth_power(int on);

#endif /* UPIO_H */

