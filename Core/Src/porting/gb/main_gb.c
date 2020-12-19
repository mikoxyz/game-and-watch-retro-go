#include <odroid_system.h>
#include <string.h>

#include "main.h"
#include "bilinear.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "gnuboy/loader.h"
#include "gnuboy/hw.h"
#include "gnuboy/lcd.h"
#include "gnuboy/cpu.h"
#include "gnuboy/mem.h"
#include "gnuboy/sound.h"
#include "gnuboy/regs.h"
#include "gnuboy/rtc.h"
#include "gnuboy/defs.h"
#include "common.h"
#include "rom_manager.h"

#define APP_ID 20

#define NVS_KEY_SAVE_SRAM "sram"


static uint32_t pause_pressed;
static uint32_t power_pressed;


static odroid_video_frame_t update1 = {GB_WIDTH, GB_HEIGHT, GB_WIDTH * 2, 2, 0xFF, -1, NULL, NULL, 0, {}};
static odroid_video_frame_t update2 = {GB_WIDTH, GB_HEIGHT, GB_WIDTH * 2, 2, 0xFF, -1, NULL, NULL, 0, {}};
static odroid_video_frame_t *currentUpdate = &update1;

static bool fullFrame = false;
static uint skipFrames = 0;

static bool netplay = false;

static bool saveSRAM = false;
static int  saveSRAM_Timer = 0;

// 3 pages
uint8_t state_save_buffer[192 * 1024] __attribute__((section (".emulator_data")));


// --- MAIN


static void netplay_callback(netplay_event_t event, void *arg)
{
    // Where we're going we don't need netplay!
}

#define WIDTH 320

// int[] resizePixels(int[] pixels,int w1,int h1,int w2,int h2) {
//     int[] temp = new int[w2*h2] ;
//     // EDIT: added +1 to account for an early rounding problem
//     int x_ratio = (int)((w1<<16)/w2) +1;
//     int y_ratio = (int)((h1<<16)/h2) +1;
//     //int x_ratio = (int)((w1<<16)/w2) ;
//     //int y_ratio = (int)((h1<<16)/h2) ;
//     int x2, y2 ;
//     for (int i=0;i<h2;i++) {
//         for (int j=0;j<w2;j++) {
//             x2 = ((j*x_ratio)>>16) ;
//             y2 = ((i*y_ratio)>>16) ;
//             temp[(i*w2)+j] = pixels[(y2*w1)+x2] ;
//         }                
//     }                
//     return temp ;
// }

static uint32_t skippedFrames = 0;


__attribute__((optimize("unroll-loops")))
static inline void screen_blit(void) {
    static uint32_t lastFPSTime = 0;
    static uint32_t lastTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        int fps = (10000 * frames) / delta;
        printf("FPS: %d.%d, frames %d, delta %d ms, skipped %d\n", fps / 10, fps % 10, delta, frames, skippedFrames);
        frames = 0;
        skippedFrames = 0;
        lastFPSTime = currentTime;
    }

    lastTime = currentTime;



    int w1 = currentUpdate->width;
    int h1 = currentUpdate->height;
    int w2 = 266;
    int h2 = 240;

    int x_ratio = (int)((w1<<16)/w2) +1;
    int y_ratio = (int)((h1<<16)/h2) +1;
    int hpad = 27;
    //int x_ratio = (int)((w1<<16)/w2) ;
    //int y_ratio = (int)((h1<<16)/h2) ;
    int x2, y2 ;
    uint16_t* screen_buf = (uint16_t*)currentUpdate->buffer;
    uint16_t *dest = lcd_get_active_buffer();

    PROFILING_INIT(t_blit);
    PROFILING_START(t_blit);

    for (int i=0;i<h2;i++) {
        for (int j=0;j<w2;j++) {
            x2 = ((j*x_ratio)>>16) ;
            y2 = ((i*y_ratio)>>16) ;
            uint16_t b2 = screen_buf[(y2*w1)+x2];
            dest[(i*WIDTH)+j+hpad] = b2;
        }
    }

    PROFILING_END(t_blit);

#ifdef PROFILING_ENABLED
    printf("Blit: %d us\n", (1000000 * PROFILING_DIFF(t_blit)) / t_blit_t0.SecondFraction);
#endif

    lcd_swap();
}

static void screen_blit_bilinear(void) {
    static uint32_t lastFPSTime = 0;
    static uint32_t lastTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        int fps = (10000 * frames) / delta;
        printf("FPS: %d.%d, frames %d, delta %d ms, skipped %d\n", fps / 10, fps % 10, delta, frames, skippedFrames);
        frames = 0;
        skippedFrames = 0;
        lastFPSTime = currentTime;
    }

    lastTime = currentTime;

    int w1 = currentUpdate->width;
    int h1 = currentUpdate->height;
    // int w2 = 266;
    // int h2 = 240;

    int w2 = 320;
    int h2 = 216;

    int x_ratio = (int)((w1<<16)/w2) +1;
    int y_ratio = (int)((h1<<16)/h2) +1;
    int hpad = 0;
    //int x_ratio = (int)((w1<<16)/w2) ;
    //int y_ratio = (int)((h1<<16)/h2) ;
    int x2, y2 ;
    uint16_t* screen_buf = (uint16_t*)currentUpdate->buffer;
    uint16_t *dest = lcd_get_active_buffer();


    image_t dst_img;
    dst_img.w = 320;
    dst_img.h = 240;
    dst_img.bpp = 2;
    dst_img.pixels = dest;


    image_t src_img;
    src_img.w = currentUpdate->width;
    src_img.h = currentUpdate->height;
    src_img.bpp = 2;
    src_img.pixels = currentUpdate->buffer;

    float x_scale = ((float) w2) / ((float) w1);
    float y_scale = ((float) h2) / ((float) h1);




    PROFILING_INIT(t_blit);
    PROFILING_START(t_blit);

    imlib_draw_image(&dst_img, &src_img, hpad, 0, x_scale, y_scale, NULL, -1, 255, NULL,
                     NULL, IMAGE_HINT_BILINEAR, NULL, NULL);

    PROFILING_END(t_blit);

#ifdef PROFILING_ENABLED
    printf("Blit: %d us\n", (1000000 * PROFILING_DIFF(t_blit)) / t_blit_t0.SecondFraction);
#endif

    lcd_swap();
}


__attribute__((optimize("unroll-loops")))
static inline void screen_blit_jth(void) {
    static uint32_t lastFPSTime = 0;
    static uint32_t lastTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        int fps = (10000 * frames) / delta;
        printf("FPS: %d.%d, frames %d, delta %d ms, skipped %d\n", fps / 10, fps % 10, delta, frames, skippedFrames);
        frames = 0;
        skippedFrames = 0;
        lastFPSTime = currentTime;
    }

    lastTime = currentTime;

    uint16_t* screen_buf = (uint16_t*)currentUpdate->buffer;
    uint16_t *dest = lcd_get_active_buffer();

    PROFILING_INIT(t_blit);
    PROFILING_START(t_blit);


    int w1 = currentUpdate->width;
    int h1 = currentUpdate->height;
    int w2 = 320;
    int h2 = 240;

    int y_done = 0;

    int y = 0;
    const int border = 24;

    // Iterate on dest buf rows
    int src_y = 0;
    for (int y = 0; y < h2; y++) {
        uint16_t *src_row  = &screen_buf[src_y * w1];
        uint16_t *dest_row = &dest[y * w2];
        if (y >= border && y <= (240 - border)) {
            for (int x = 0; x < w1; x++) {
                dest_row[2 * x]     = src_row[x];
                dest_row[2 * x + 1] = src_row[x];
            }
            if (y & 1) {
                src_y++;
            }
        } else {
            for (int x = 0; x < w1; x++) {
                dest_row[2 * x]     = src_row[x];
                dest_row[2 * x + 1] = src_row[x];
            }
            src_y++;
        }
    }

    PROFILING_END(t_blit);

#ifdef PROFILING_ENABLED
    printf("Blit: %d us\n", (1000000 * PROFILING_DIFF(t_blit)) / t_blit_t0.SecondFraction);
#endif

    lcd_swap();
}

// __attribute__((optimize("unroll-loops")))
// static inline void screen_blit(void)
// {
//     // printf("%d x %d\n", bmp->width, bmp->height);
//     for (int y = 0; y < currentUpdate->height; y++) {
//         // uint8_t *row = bmp->line[line];
//         for (int x = 0; x < currentUpdate->width; x++) {
//             uint16_t* screen_buf = (uint16_t*)currentUpdate->buffer;
//             uint16_t b1 = screen_buf[(currentUpdate->width * y + x)];
//             // uint16_t b2 = screen_buf[(WIDTH * y + x)*2 + 1];
//             // for (int line = 0; line < bmp->height; line++) {
//             //     uint8_t *row = bmp->line[line];
//             //     for (int x = 0; x < bmp->width; x++) {
//             //         framebuffer1[WIDTH * line + x + hpad] = myPalette[row[x] & 0b111111];
//             //     }
//             // }
//             // framebuffer1[WIDTH * y + x] = currentUpdate->palette[b1];
//             framebuffer1[WIDTH * (y+48) + x + 80] = b1;
//         }
//     }
//     /*
//     odroid_video_frame_t *previousUpdate = (currentUpdate == &update1) ? &update2 : &update1;

//     fullFrame = odroid_display_queue_update(currentUpdate, previousUpdate) == SCREEN_UPDATE_FULL;

//     // swap buffers
//     currentUpdate = previousUpdate;
//     fb.ptr = currentUpdate->buffer;
//     */
// }


static bool SaveState(char *pathName)
{
    printf("Saving state...\n");

    memset(state_save_buffer, '\x00', sizeof(state_save_buffer));
    gb_state_save(state_save_buffer, sizeof(state_save_buffer));
    store_save(ACTIVE_FILE->save_address, state_save_buffer, sizeof(state_save_buffer));

    return 0;
}

static bool LoadState(char *pathName)
{
    gb_state_load(ACTIVE_FILE->save_address, ACTIVE_FILE->save_size);
    return true;
}


static bool palette_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event)
{
    /* what?! */
    /*int pal = pal_get_dmg();
    int max = pal_count_dmg();

    if (event == ODROID_DIALOG_PREV) {
        pal = pal > 0 ? pal - 1 : max;
    }

    if (event == ODROID_DIALOG_NEXT) {
        pal = pal < max ? pal + 1 : 0;
    }

    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
        odroid_settings_Palette_set(pal);
        pal_set_dmg(pal);
        emu_run(true);
    }

    if (pal == 0) strcpy(option->value, "GBC");
    else sprintf(option->value, "%d/%d", pal, max);

    return event == ODROID_DIALOG_ENTER;*/
    return false;
}

/*static bool save_sram_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event)
{
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
        saveSRAM = !saveSRAM;
        odroid_settings_app_int32_set(NVS_KEY_SAVE_SRAM, saveSRAM);
    }

    strcpy(option->value, saveSRAM ? "Yes" : "No");

    return event == ODROID_DIALOG_ENTER;
    return -
}

static bool rtc_t_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event)
{
    if (option->id == 'd') {
        if (event == ODROID_DIALOG_PREV && --rtc.d < 0) rtc.d = 364;
        if (event == ODROID_DIALOG_NEXT && ++rtc.d > 364) rtc.d = 0;
        sprintf(option->value, "%03d", rtc.d);
    }
    if (option->id == 'h') {
        if (event == ODROID_DIALOG_PREV && --rtc.h < 0) rtc.h = 23;
        if (event == ODROID_DIALOG_NEXT && ++rtc.h > 23) rtc.h = 0;
        sprintf(option->value, "%02d", rtc.h);
    }
    if (option->id == 'm') {
        if (event == ODROID_DIALOG_PREV && --rtc.m < 0) rtc.m = 59;
        if (event == ODROID_DIALOG_NEXT && ++rtc.m > 59) rtc.m = 0;
        sprintf(option->value, "%02d", rtc.m);
    }
    if (option->id == 's') {
        if (event == ODROID_DIALOG_PREV && --rtc.s < 0) rtc.s = 59;
        if (event == ODROID_DIALOG_NEXT && ++rtc.s > 59) rtc.s = 0;
        sprintf(option->value, "%02d", rtc.s);
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool rtc_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event)
{
    if (event == ODROID_DIALOG_ENTER) {
        static odroid_dialog_choice_t choices[] = {
            {'d', "Day", "000", 1, &rtc_t_update_cb},
            {'h', "Hour", "00", 1, &rtc_t_update_cb},
            {'m', "Min",  "00", 1, &rtc_t_update_cb},
            {'s', "Sec",  "00", 1, &rtc_t_update_cb},
            ODROID_DIALOG_CHOICE_LAST
        };
        odroid_overlay_dialog("Set Clock", choices, 0);
    }
    sprintf(option->value, "%02d:%02d", rtc.h, rtc.m);
    return false;
}

static bool advanced_settings_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event)
{
   if (event == ODROID_DIALOG_ENTER) {
      odroid_dialog_choice_t options[] = {
        {101, "Set clock", "00:00", 1, &rtc_update_cb},
        {102, "Auto save SRAM", "No", 1, &save_sram_update_cb},
        ODROID_DIALOG_CHOICE_LAST
      };
      odroid_overlay_dialog("Advanced", options, 0);
   }
   return false;
}*/

// Hacky but it works: Locate the framebuffer in ITCRAM
uint8_t gb_buffer1[GB_WIDTH * GB_HEIGHT * 2]  __attribute__((section (".itcram_data")));

void pcm_submit() {
    uint8_t volume = odroid_audio_volume_get();
    uint8_t shift = ODROID_AUDIO_VOLUME_MAX - volume + 1;
    size_t offset = (dma_state == DMA_TRANSFER_STATE_HF) ? 0 : AUDIO_BUFFER_LENGTH;

    if (audio_mute || volume == ODROID_AUDIO_VOLUME_MIN) {
        for (int i = 0; i < AUDIO_BUFFER_LENGTH; i++) {
            audiobuffer_dma[i + offset] = 0;
        }
    } else {
        for (int i = 0; i < AUDIO_BUFFER_LENGTH; i++) {
            audiobuffer_dma[i + offset] = pcm.buf[i] >> shift;
        }
    }
}

bool odroid_netplay_quick_start(void)
{
    return true;
}

// TODO: Move to own file
void odroid_audio_mute(bool mute)
{
    if (mute) {
        for (int i = 0; i < sizeof(audiobuffer_dma) / sizeof(audiobuffer_dma[0]); i++) {
            audiobuffer_dma[i] = 0;
        }
    }

    audio_mute = mute;
}


rg_app_desc_t * init(uint8_t load_state) {
    odroid_gamepad_state_t joystick;

    odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &netplay_callback);

    // Hack: Use the same buffer twice
    update1.buffer = gb_buffer1;
    update2.buffer = gb_buffer1;

    //saveSRAM = odroid_settings_app_int32_get(NVS_KEY_SAVE_SRAM, 0);
    saveSRAM = false;

    // Load ROM
    loader_init(NULL);

    // RTC
    memset(&rtc, 0, sizeof(rtc));

    // Video
    memset(framebuffer1, 0, sizeof(framebuffer1));
    memset(framebuffer2, 0, sizeof(framebuffer2));
    memset(&fb, 0, sizeof(fb));
    fb.w = GB_WIDTH;
  	fb.h = GB_HEIGHT;
  	fb.pixelsize = 2;
  	fb.pitch = fb.w * fb.pixelsize;
  	fb.ptr = currentUpdate->buffer;
  	fb.enabled = 1;
    fb.byteorder = 1;
    // fb.blit_func = &screen_blit;
    // fb.blit_func = &screen_blit_bilinear;
    fb.blit_func = &screen_blit_jth;

    // Audio
    memset(audiobuffer_emulator, 0, sizeof(audiobuffer_emulator));
    memset(&pcm, 0, sizeof(pcm));
    pcm.hz = AUDIO_SAMPLE_RATE;
    pcm.stereo = 0;
    pcm.len = AUDIO_BUFFER_LENGTH;
    pcm.buf = (n16*)&audiobuffer_emulator;
    pcm.pos = 0;

    memset(audiobuffer_dma, 0, sizeof(audiobuffer_dma));
    HAL_SAI_Transmit_DMA(&hsai_BlockA1, audiobuffer_dma, sizeof(audiobuffer_dma) / sizeof(audiobuffer_dma[0]));

    rg_app_desc_t *app = odroid_system_get_app();

    emu_init();

    //pal_set_dmg(odroid_settings_Palette_get());
    pal_set_dmg(2);

    // Don't load state if the pause button is held while booting
    uint32_t boot_buttons = GW_GetBootButtons();
    pause_pressed = (boot_buttons & B_PAUSE);
    power_pressed = (boot_buttons & B_POWER);

    if (load_state) {
        LoadState("");
    }
    return app;
}

void app_main_gb(uint8_t load_state)
{
    rg_app_desc_t *app = init(load_state);
    odroid_gamepad_state_t joystick;


    const int frameTime = get_frame_time(60);

    while (true)
    {


        odroid_input_read_gamepad(&joystick);

        if (joystick.values[ODROID_INPUT_VOLUME]) {

            // TODO: Sync framebuffers in a nicer way
            lcd_sync();

            odroid_dialog_choice_t options[] = {
                {300, "Palette", "7/7", !hw.cgb, &palette_update_cb},
                // {301, "More...", "", 1, &advanced_settings_cb},
                ODROID_DIALOG_CHOICE_LAST
            };
            odroid_overlay_game_menu(options);

        }
        // else if (joystick.values[ODROID_INPUT_VOLUME]) {
        //     odroid_dialog_choice_t options[] = {
        //         {100, "Palette", "7/7", !hw.cgb, &palette_update_cb},
        //         // {101, "More...", "", 1, &advanced_settings_cb},
        //         ODROID_DIALOG_CHOICE_LAST
        //     };
        //     odroid_overlay_game_settings_menu(options);
        // }

        uint startTime = get_elapsed_time();
        bool drawFrame = !skipFrames;

        pad_set(PAD_UP, joystick.values[ODROID_INPUT_UP]);
        pad_set(PAD_RIGHT, joystick.values[ODROID_INPUT_RIGHT]);
        pad_set(PAD_DOWN, joystick.values[ODROID_INPUT_DOWN]);
        pad_set(PAD_LEFT, joystick.values[ODROID_INPUT_LEFT]);
        pad_set(PAD_SELECT, joystick.values[ODROID_INPUT_SELECT]);
        pad_set(PAD_START, joystick.values[ODROID_INPUT_START]);
        pad_set(PAD_A, joystick.values[ODROID_INPUT_A]);
        pad_set(PAD_B, joystick.values[ODROID_INPUT_B]);

        if (power_pressed != joystick.values[ODROID_INPUT_POWER]) {
            printf("Power toggle %d=>%d\n", power_pressed, !power_pressed);
            power_pressed = joystick.values[ODROID_INPUT_POWER];
            if (power_pressed) {
                printf("Power PRESSED %d\n", power_pressed);
                HAL_SAI_DMAStop(&hsai_BlockA1);
                lcd_backlight_off();

                if(!joystick.values[ODROID_INPUT_VOLUME]) {
                    // Always save as long as PAUSE is not pressed
                    SaveState("");
                }

                GW_EnterDeepSleep();
            }
        }

        emu_run(drawFrame);

        if (saveSRAM)
        {
            if (ram.sram_dirty)
            {
                saveSRAM_Timer = 120; // wait 2 seconds
                ram.sram_dirty = 0;
            }

            if (saveSRAM_Timer > 0 && --saveSRAM_Timer == 0)
            {
                // TO DO: Try compressing the sram file, it might reduce stuttering
                sram_save();
            }
        }

        if (skipFrames == 0)
        {
            if (get_elapsed_time_since(startTime) > frameTime) skipFrames = 1;
            if (app->speedupEnabled) {
                skipFrames += app->speedupEnabled * 2;
                skippedFrames += app->speedupEnabled * 2;
            }
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }

        // Tick before submitting audio/syncing
        odroid_system_tick(!drawFrame, fullFrame, get_elapsed_time_since(startTime));

        if (!app->speedupEnabled)
        {
            // odroid_audio_submit(pcm.buf, pcm.pos >> 1);
            // handled in pcm_submit instead.
            static dma_transfer_state_t last_dma_state = DMA_TRANSFER_STATE_HF;
            while (dma_state == last_dma_state) {
                __NOP();
            }
            last_dma_state = dma_state;
        }
    }
}
