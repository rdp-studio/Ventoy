/******************************************************************************
 * Language.c
 *
 * Copyright (c) 2020, longpanda <admin@ventoy.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */
 
#include <Windows.h>
#include "Ventoy2Disk.h"
#include "Language.h"

const TCHAR * g_Str_English[STR_ID_MAX] =
{
    TEXT("Error"),
    TEXT("Warning"),
    TEXT("Info"),
    TEXT("Please run under the correct directory!"),
    TEXT("Device"),
    TEXT("Ventoy At Local"),
    TEXT("Ventoy In Device"),
    TEXT("Status - READY"),
    TEXT("Install"),
    TEXT("Update"),
    TEXT("Upgrade operation is safe, ISO files will be unchanged.\r\nContinue?"),
    TEXT("The disk will be formatted and all the data will be lost.\r\nContinue?"),
    TEXT("The disk will be formatted and all the data will be lost.\r\nContinue? (Double Check)"),
    TEXT("Congratulations!\r\nVentoy has been successfully installed to the device."),
    TEXT("An error occurred during the installation. Please check log.txt for detail."),
    TEXT("Congratulations!\r\nVentoy has been successfully updated to the device."),
    TEXT("An error occurred during the update. Please check log.txt for detail."),

    TEXT("A thread is running, please wait..."),
};

const TCHAR * g_Str_ChineseSimple[STR_ID_MAX] =
{
    TEXT("����"),
    TEXT("����"),
    TEXT("����"),
    TEXT("������ȷ��Ŀ¼������!"),
    TEXT("�豸"),
    TEXT("���� Ventoy"),
    TEXT("�豸�� Ventoy"),
    TEXT("״̬ - ׼������"),
    TEXT("��װ"),
    TEXT("����"),
    TEXT("���������ǰ�ȫ��, ISO�ļ����ᶪʧ\r\n�Ƿ������"),
    TEXT("���̻ᱻ��ʽ��, �������ݶ��ᶪʧ!\r\n�Ƿ������"),
    TEXT("���̻ᱻ��ʽ��, �������ݶ��ᶪʧ!\r\n�ٴ�ȷ���Ƿ������"),
    TEXT("��ϲ��! Ventoy �Ѿ��ɹ���װ�����豸��."),
    TEXT("��װ Ventoy �����з�������. ��ϸ��Ϣ����� log.txt �ļ�."),
    TEXT("��ϲ��! �°汾�� Ventoy �Ѿ��ɹ����µ����豸��."),
    TEXT("���� Ventoy ��������������. ��ϸ��Ϣ����� log.txt �ļ�."),

    TEXT("��ǰ��������������, ��ȴ�..."),
};

const TCHAR * GetString(enum STR_ID ID)
{
    return g_Str_English[ID];
};
