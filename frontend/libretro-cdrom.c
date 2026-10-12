#include "libretro-cdrom.h"
#include "../deps/libretro-common/cdrom/cdrom.c"
#if defined(__linux__) && !defined(ANDROID)
//#include <linux/cdrom.h>
#endif

#include "../libpcsxcore/psxcommon.h"
#include "../libpcsxcore/cdrom.h"

//#include "vfs/vfs_implementation.h"
#include "vfs/vfs_implementation_cdrom.h"

static int cdrom_send_command_dummy(const libretro_vfs_implementation_file *stream,
      CDROM_CMD_Direction dir, void *buf, size_t len, unsigned char *cmd, size_t cmd_len,
      unsigned char *sense, size_t sense_len)
{
   return 1;
}

static int cdrom_send_command_once(const libretro_vfs_implementation_file *stream,
      CDROM_CMD_Direction dir, void *buf, size_t len, unsigned char *cmd, size_t cmd_len)
{
   unsigned char sense[CDROM_MAX_SENSE_BYTES] = {0};
   int ret =
#if defined(__linux__) && !defined(ANDROID)
      cdrom_send_command_linux
#elif defined(_WIN32) && !defined(_XBOX)
      cdrom_send_command_win32
#else
      cdrom_send_command_dummy
#endif
         (stream, dir, buf, len, cmd, cmd_len, sense, sizeof(sense));
#ifdef CDROM_DEBUG
   if (ret && sense[2])
      cdrom_print_sense_data(sense, sizeof(sense));
#endif
   (void)cdrom_send_command_dummy;
   return ret;
}

// "extensions" to libretro-common
int cdrom_set_read_speed_x(libretro_vfs_implementation_file *stream, unsigned speed)
{
   // SET CD-ROM SPEED, DA is newer?
   unsigned char cmd1[] = {0xDA, 0, speed - 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
   unsigned char cmd2[] = {0xBB, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
   int ret;
   ret = cdrom_send_command_once(stream, DIRECTION_NONE, NULL, 0, cmd1, sizeof(cmd1));
   if (ret) {
#if defined(__linux__) && !defined(ANDROID)
      // doesn't work, too late?
      //ret = ioctl(fileno(stream->fp), CDROM_SELECT_SPEED, &speed);
#endif
   }
   if (ret) {
      speed = speed * 2352 * 75 / 1024;
      cmd2[2] = speed >> 8;
      cmd2[3] = speed;
      ret = cdrom_send_command_once(stream, DIRECTION_NONE, NULL, 0, cmd2, sizeof(cmd2));
   }
   return ret;
}

int rcdrom_readSector(void *stream, unsigned int lba, void *b)
{
   unsigned char cmd[] = {0xBE, 0, 0, 0, 0, 0, 0, 0, 1, 0xF8, 0, 0};
   cmd[2] = lba >> 24;
   cmd[3] = lba >> 16;
   cmd[4] = lba >> 8;
   cmd[5] = lba;
   return cdrom_send_command_once(stream, DIRECTION_IN, b, 2352, cmd, sizeof(cmd));
}

/* The subchannel (LibCrypt reads Q): READ CD with raw P-W, 96 bytes, Q in bit 6 of each byte.
 * sub_mode 1 = sub only; 2 = the sector + sub (2448 B, for drives that refuse sub alone); 0 = none.
 * Some drives hand the Q of a neighbouring sector (one ahead on the first drive tried): sub_shift
 * is that distance, and the read asks for lba - sub_shift so the Q matches lba.
 * Q is decoded into the SubQ layout: 12 bytes at offset 12 (as cdriso's DecodeRawSubData). */
static int sub_mode, sub_shift;

static int rcdrom_readRawSub(void *stream, unsigned int lba, unsigned char *sub, int mode)
{
   unsigned char cmd[] = {0xBE, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0x01, 0};
   unsigned char buf[2352 + 96];
   int ret;
   cmd[2] = lba >> 24;
   cmd[3] = lba >> 16;
   cmd[4] = lba >> 8;
   cmd[5] = lba;
   if (mode == 1)
      return cdrom_send_command_once(stream, DIRECTION_IN, sub, 96, cmd, sizeof(cmd));
   cmd[9] = 0xF8;
   ret = cdrom_send_command_once(stream, DIRECTION_IN, buf, sizeof(buf), cmd, sizeof(cmd));
   if (!ret)
      memcpy(sub, buf + 2352, 96);
   return ret;
}

static void decode_q(unsigned char *sub)
{
   unsigned char q[12];
   int i;
   memset(q, 0, sizeof(q));
   for (i = 0; i < 8 * 12; i++)
      if (sub[i] & (1 << 6))
         q[i >> 3] |= 1 << (7 - (i & 7));
   memcpy(sub + 12, q, 12);
}

int rcdrom_readSub(void *stream, unsigned int lba, void *b)
{
   int ret;
   if (!sub_mode || (int)lba < sub_shift)
      return -1;
   ret = rcdrom_readRawSub(stream, lba - sub_shift, b, sub_mode);
   if (ret)
      return -1;
   decode_q(b);
   return 0;
}

/* the absolute address in a decoded Q as an lba, -1 when it is not an ADR 1 (position) entry */
static int q_lba(const unsigned char *sub)
{
   if ((sub[12] & 0x0f) != 1)
      return -1;
   return (btoi(sub[19]) * 60 + btoi(sub[20])) * 75 + btoi(sub[21]) - 150;
}

/* try both ways on data sectors with no LibCrypt marks (16..19, the system area): the Q must carry
 * ADR 1 and an address within 2 of the sector read, the same distance on every sector; a drive that
 * is still spinning up fails the first reads, so each way gets a few tries */
static void probe_subchannel(void *stream)
{
   static const char *names[] = { "", "sub only", "sector + sub" };
   unsigned char sub[96];
   int mode, tries, lba, shift;
   for (mode = 1; mode <= 2; mode++) {
      for (tries = 0; tries < 3; tries++) {
         shift = -100;
         for (lba = 16; lba < 20; lba++) {
            int got, d;
            if (rcdrom_readRawSub(stream, lba, sub, mode))
               break;
            decode_q(sub);
            got = q_lba(sub);
            d = got - lba;
            if (got < 0 || d < -2 || d > 2 || (shift != -100 && d != shift)) {
               SysPrintf("CD subchannel (%s): lba %d gave Q %02x %02x:%02x:%02x\n", names[mode],
                     lba, sub[12], sub[19], sub[20], sub[21]);
               break;
            }
            shift = d;
         }
         if (lba == 20) {
            sub_mode = mode;
            sub_shift = shift;
            SysPrintf("CD subchannel: raw P-W, %s, Q shifted by %d\n", names[mode], shift);
            return;
         }
      }
   }
   sub_mode = 0;
   SysPrintf("CD subchannel: not readable, LibCrypt discs need an .sbi\n");
}

void *rcdrom_open(const char *name, u32 *total_lba, u32 *have_subchannel)
{
   void *g_cd_handle = retro_vfs_file_open_impl(name, RETRO_VFS_FILE_ACCESS_READ,
        RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!g_cd_handle) {
      SysPrintf("retro_vfs_file_open failed for '%s'\n", name);
      return NULL;
   }
   else {
      int ret = cdrom_set_read_speed_x(g_cd_handle, 4);
      if (ret) SysPrintf("CD speed set failed\n");
      const cdrom_toc_t *toc = retro_vfs_file_get_cdrom_toc();
      const cdrom_track_t *last = &toc->track[toc->num_tracks - 1];
      unsigned int lba = MSF2SECT(last->min, last->sec, last->frame);
      *total_lba = lba + last->track_size;
      probe_subchannel(g_cd_handle);
      *have_subchannel = sub_mode != 0;
      //cdrom_get_current_config_random_readable(acdrom.h);
      //cdrom_get_current_config_multiread(acdrom.h);
      //cdrom_get_current_config_cdread(acdrom.h);
      //cdrom_get_current_config_profiles(acdrom.h);
      return g_cd_handle;
   }
}

void rcdrom_close(void *stream)
{
   retro_vfs_file_close_impl(stream);
}

int rcdrom_getTN(void *stream, u8 *tn)
{
   const cdrom_toc_t *toc = retro_vfs_file_get_cdrom_toc();
   if (toc) {
     tn[0] = 1;
     tn[1] = toc->num_tracks;
     return 0;
   }
   return -1;
}

int rcdrom_getTD(void *stream, u32 total_lba, u8 track, u8 *rt)
{
   const cdrom_toc_t *toc = retro_vfs_file_get_cdrom_toc();
   rt[0] = 0, rt[1] = 2, rt[2] = 0;
   if (track == 0) {
      lba2msf(total_lba + 150, &rt[0], &rt[1], &rt[2]);
   }
   else if (track <= toc->num_tracks) {
      int i = track - 1;
      rt[0] = toc->track[i].min;
      rt[1] = toc->track[i].sec;
      rt[2] = toc->track[i].frame;
   }
   return 0;
}

int rcdrom_getStatus(void *stream, struct CdrStat *stat)
{
   const cdrom_toc_t *toc = retro_vfs_file_get_cdrom_toc();
   stat->Type = toc->track[0].audio ? 2 : 1;
   return 0;
}

int rcdrom_isMediaInserted(void *stream)
{
   return cdrom_is_media_inserted(stream);
}

// vim:sw=3:ts=3:expandtab
