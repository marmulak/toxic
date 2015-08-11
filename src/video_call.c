/*  video_call.c
 *
 *
 *  Copyright (C) 2014 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic.
 *
 *  Toxic is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Toxic is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Toxic.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "toxic.h"
#include "windows.h"
#include "video_call.h"
#include "video_device.h"
#include "chat_commands.h"
#include "global_commands.h"
#include "line_info.h"
#include "notify.h"

#include <stdbool.h>
#include <curses.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

void receive_video_frame_cb( ToxAV *av, uint32_t friend_number,
                                    uint16_t width, uint16_t height,
                                    uint8_t const *y, uint8_t const *u, uint8_t const *v,
                                    int32_t ystride, int32_t ustride, int32_t vstride,
                                    void *user_data );
void video_bit_rate_status_cb( ToxAV *av, uint32_t friend_number, 
                                      bool stable, uint32_t bit_rate, void *user_data);

static void print_err (ToxWindow *self, const char *error_str)
{
    line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "%s", error_str);
}

ToxAV *init_video(ToxWindow *self, Tox *tox)
{
    CallControl.video_errors = ve_None;

    CallControl.video_enabled = true;
    CallControl.video_bit_rate = 5000;
    CallControl.video_frame_duration = 10;
    CallControl.video_call = vs_None;

    if ( !CallControl.av ) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "Video failed to init with ToxAV instance");

        return NULL;
    }

    if ( init_video_devices(CallControl.av) == vde_InternalError ) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "Failed to init video devices");

        return NULL;
    }

    toxav_callback_video_receive_frame(CallControl.av, receive_video_frame_cb, &CallControl);
    toxav_callback_video_bit_rate_status(CallControl.av, video_bit_rate_status_cb, &CallControl);

    return CallControl.av;
}

void terminate_video()
{
    int i;
    for (i = 0; i < MAX_CALLS; ++i)
        stop_video_transmission(&CallControl.calls[i], i);

    terminate_video_devices();
}

void read_video_device_callback(int16_t width, int16_t height, const uint8_t* y, const uint8_t* u, const uint8_t* v, void* data)
{
    uint32_t friend_number = *((uint32_t*)data); /* TODO: Or pass an array of call_idx's */
    TOXAV_ERR_SEND_FRAME error;

    if ( toxav_video_send_frame(CallControl.av, friend_number, width, height, y, u, v, &error ) == false ) {
        line_info_add(CallControl.prompt, NULL, NULL, NULL, SYS_MSG, 0, 0, "Failed to send video frame");

        if ( error == TOXAV_ERR_SEND_FRAME_NULL )
            line_info_add(CallControl.prompt, NULL, NULL, NULL, SYS_MSG, 0, 0, "Failed to capture video frame");
        else if ( error == TOXAV_ERR_SEND_FRAME_INVALID )
            line_info_add(CallControl.prompt, NULL, NULL, NULL, SYS_MSG, 0, 0, "Failed to prepare video frame");
    }
}

void write_video_device_callback(uint32_t friend_number, uint16_t width, uint16_t height,
                                           uint8_t const *y, uint8_t const *u, uint8_t const *v,
                                           int32_t ystride, int32_t ustride, int32_t vstride,
                                           void *user_data)
{
    if (write_video_out(width, height, y, u, v, ystride, ustride, vstride, user_data) == vde_DeviceNotActive) 
        callback_recv_video_starting(friend_number);
}

int start_video_transmission(ToxWindow *self, ToxAV *av, Call *call)
{
    if ( !self || !av) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "Failed to prepare transmission");
        return -1;
    }

    if (toxav_video_bit_rate_set(CallControl.av, self->num, CallControl.video_bit_rate, true, NULL) == false) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "Failed to set video bit rate");
        return -1;
    }

    if ( open_primary_video_device(vdt_input, &call->vin_idx) != vde_None ) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "Failed to open input video device!");
        return -1;
    }
    
    if ( register_video_device_callback(self->num, call->vin_idx, read_video_device_callback, &self->num) != vde_None)
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "Failed to register input video handler!");

    return 0;
}

int stop_video_transmission(Call *call, int friend_number)
{
    if (toxav_video_bit_rate_set(CallControl.av, friend_number, 0, true, NULL)) {
        return -1;
    }

    if ( call->vin_idx != -1 )
        close_video_device(vdt_input, call->vin_idx);

    if ( call->vout_idx != -1 )
        close_video_device(vdt_output, call->vout_idx);

    return 0;
}
/*
 * End of transmission
 */





/*
 * Callbacks
 */
void receive_video_frame_cb(ToxAV *av, uint32_t friend_number,
                                    uint16_t width, uint16_t height,
                                    uint8_t const *y, uint8_t const *u, uint8_t const *v,
                                    int32_t ystride, int32_t ustride, int32_t vstride,
                                    void *user_data)
{
    write_video_device_callback(friend_number, width, height, y, u, v, ystride, ustride, vstride, user_data);
}

void video_bit_rate_status_cb(ToxAV *av, uint32_t friend_number, 
                                      bool stable, uint32_t bit_rate, void *user_data)
{
    if ( stable ) {
        CallControl.video_bit_rate = bit_rate;
        toxav_video_bit_rate_set(CallControl.av, friend_number, CallControl.video_bit_rate, false, NULL);
    }
}

void callback_recv_video_starting(uint32_t friend_number)
{
    Call* this_call = &CallControl.calls[friend_number];

    if ( this_call->vout_idx == -1 )
        return;

    open_primary_video_device(vdt_output, &this_call->vout_idx);

    if ( CallControl.video_call == vs_Send )
        CallControl.video_call = vs_SendReceive;
    else
        CallControl.video_call = vs_Receive;

    //line_info_add(CallControl.prompt, NULL, NULL, NULL, SYS_MSG, 0, 0, "%i recv start", CallControl.video_call);
}
void callback_recv_video_end(uint32_t friend_number)
{
    Call* this_call = &CallControl.calls[friend_number];

    close_video_device(vdt_output, this_call->vout_idx);

    if ( CallControl.video_call == vs_SendReceive )
        CallControl.video_call = vs_Send;
    else
        CallControl.video_call = vs_None;

    //line_info_add(CallControl.prompt, NULL, NULL, NULL, SYS_MSG, 0, 0, "%i recv end", CallControl.video_call);
}
void callback_video_starting(uint32_t friend_number)
{
    ToxWindow* windows = CallControl.prompt;
    Call* this_call = &CallControl.calls[friend_number];

    TOXAV_ERR_CALL_CONTROL error = TOXAV_ERR_CALL_CONTROL_OK;
    toxav_call_control(CallControl.av, friend_number, TOXAV_CALL_CONTROL_SHOW_VIDEO, &error);

    if (error == TOXAV_ERR_CALL_CONTROL_OK) {
        int i;
        for (i = 0; i < MAX_WINDOWS_NUM; ++i) {
            if ( windows[i].is_call && windows[i].num == friend_number ) {
                if(0 != start_video_transmission(&windows[i], CallControl.av, this_call)) {
                    line_info_add(&windows[i], NULL, NULL, NULL, SYS_MSG, 0, 0, "Error starting transmission!");
                    return;
                }

                line_info_add(&windows[i], NULL, NULL, NULL, SYS_MSG, 0, 0, "Video capture starting.");

                if ( CallControl.video_call == vs_Receive )
                    CallControl.video_call = vs_SendReceive;
                else
                    CallControl.video_call = vs_Send;

                //line_info_add(CallControl.prompt, NULL, NULL, NULL, SYS_MSG, 0, 0, "%i start", CallControl.video_call);
            }
        }
    }
}
void callback_video_end(uint32_t friend_number)
{
    ToxWindow* windows = CallControl.prompt;

    if ( CallControl.video_call != vs_None ) {
        int i;
        for (i = 0; i < MAX_WINDOWS_NUM; ++i)
            if ( windows[i].is_call && windows[i].num == friend_number )
                line_info_add(&windows[i], NULL, NULL, NULL, SYS_MSG, 0, 0, "Video capture ending.");

        toxav_video_bit_rate_set(CallControl.av, friend_number, 0, true, NULL);

        for (i = 0; i < MAX_CALLS; ++i)
            stop_video_transmission(&CallControl.calls[i], i);

        if ( CallControl.video_call == vs_SendReceive )
            CallControl.video_call = vs_Receive;
        else
            CallControl.video_call = vs_None;

        //line_info_add(CallControl.prompt, NULL, NULL, NULL, SYS_MSG, 0, 0, "%i end", CallControl.video_call);
    }
}
/*
 * End of Callbacks
 */



/*
 * Commands from chat_commands.h
 */
void cmd_video(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    const char *error_str;

    if ( argc != 0 ) {
        error_str = "Unknown arguments.";
        goto on_error;
    }

    if ( !CallControl.av ) {
        error_str = "ToxAV not supported!";
        goto on_error;
    }

    if ( !self->stb->connection ) {
        error_str = "Friend is offline.";
        goto on_error;
    }

    if ( !self->is_call ) {
        error_str = "Not in call!";
        goto on_error;
    }

    if ( CallControl.video_call == vs_Send || CallControl.video_call == vs_SendReceive ) {
        error_str = "Video is already sending in this call.";
        goto on_error;
    }

    callback_video_starting(self->num);

    return;
on_error:
    print_err (self, error_str);
}

void cmd_end_video(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    const char *error_str;

    if ( argc != 0 ) {
        error_str = "Unknown arguments.";
        goto on_error;
    }

    if ( !CallControl.av ) {
        error_str = "ToxAV not supported!";
        goto on_error;
    }

    if ( CallControl.video_call == vs_None ) {
        error_str = "Video is not running in this call.";
        goto on_error;
    }

    callback_video_end(self->num);

    return;
on_error:
    print_err (self, error_str);
}

void cmd_list_video_devices(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    const char *error_str;

    if ( argc != 1 ) {
        if ( argc < 1 ) error_str = "Type must be specified!";
        else error_str = "Only one argument allowed!";

        goto on_error;
    }

    VideoDeviceType type;

    if ( strcasecmp(argv[1], "in") == 0 ) /* Input devices */
        type = input;

    else if ( strcasecmp(argv[1], "out") == 0 ) /* Output devices */
        type = output;

    else {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "Invalid type: %s", argv[1]);
        return;
    }

    print_video_devices(self, type);

    return;
on_error:
    print_err (self, error_str);
}

/* This changes primary video device only */
void cmd_change_video_device(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    const char *error_str;

    if ( argc != 2 ) {
        if ( argc < 1 ) error_str = "Type must be specified!";
        else if ( argc < 2 ) error_str = "Must have id!";
        else error_str = "Only two arguments allowed!";

        goto on_error;
    }

    VideoDeviceType type;

    if ( strcmp(argv[1], "in") == 0 ) /* Input devices */
        type = input;

    else if ( strcmp(argv[1], "out") == 0 ) /* Output devices */
        type = output;

    else {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "Invalid type: %s", argv[1]);
        return;
    }


    char *end;
    long int selection = strtol(argv[2], &end, 10);

    if ( *end ) {
        error_str = "Invalid input";
        goto on_error;
    }

    if ( set_primary_video_device(type, selection) == vde_InvalidSelection ) {
        error_str = "Invalid selection!";
        goto on_error;
    }

    return;
on_error:
    print_err (self, error_str);
}

void cmd_ccur_video_device(WINDOW *window, ToxWindow *self, Tox *m, int argc, char (*argv)[MAX_STR_SIZE])
{
    const char *error_str;

    if ( argc != 2 ) {
        if ( argc < 1 ) error_str = "Type must be specified!";
        else if ( argc < 2 ) error_str = "Must have id!";
        else error_str = "Only two arguments allowed!";

        goto on_error;
    }

    VideoDeviceType type;

    if ( strcmp(argv[1], "in") == 0 ) /* Input devices */
        type = input;

    else if ( strcmp(argv[1], "out") == 0 ) /* Output devices */
        type = output;

    else {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, "Invalid type: %s", argv[1]);
        return;
    }


    char *end;
    long int selection = strtol(argv[2], &end, 10);

    if ( *end ) {
        error_str = "Invalid input";
        goto on_error;
    }

    if ( video_selection_valid(type, selection) == vde_InvalidSelection ) {
        error_str="Invalid selection!";
        goto on_error;
    }

    /* If call is active, change device */
    if ( self->is_call ) {
        Call* this_call = &CallControl.calls[self->num];
        if ( this_call->ttas ) {

            if ( type == vdt_output ) {
            }
            else {
                /* TODO: check for failure */
                close_video_device(input, this_call->vin_idx);
                open_video_device(input, selection, &this_call->vin_idx);
                register_video_device_callback(self->num, this_call->vin_idx, read_video_device_callback, &self->num);
            }
        }
    }

    self->video_device_selection[type] = selection;

    return;
    on_error:
    print_err (self, error_str);
}