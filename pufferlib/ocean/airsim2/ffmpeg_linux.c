#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <raylib.h>

#include "ffmpeg.h"

#define READ_END 0
#define WRITE_END 1

struct FFMPEG {
    int pipe;
    pid_t pid;
};

FFMPEG *ffmpeg_start_rendering(size_t width, size_t height, size_t fps, const char *sound_file_path)
{
    int pipefd[2];

    // Create pipe before fork
    if (pipe(pipefd) < 0) {
        TraceLog(LOG_ERROR, "FFMPEG: Could not create a pipe: %s", strerror(errno));
        return NULL;
    }

    pid_t child = fork();
    if (child < 0) {
        TraceLog(LOG_ERROR, "FFMPEG: could not fork a child: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    if (child == 0) {
        // In child process
        close(pipefd[WRITE_END]);  // Close write end in child

        // Redirect stdin to pipe read end
        if (dup2(pipefd[READ_END], STDIN_FILENO) < 0) {
            TraceLog(LOG_ERROR, "FFMPEG CHILD: could not redirect stdin: %s", strerror(errno));
            exit(1);
        }
        close(pipefd[READ_END]);  // Original fd no longer needed

        // Redirect stdout/stderr to /dev/null to prevent console output
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        char resolution[64];
        snprintf(resolution, sizeof(resolution), "%zux%zu", width, height);
        char framerate[64];
        snprintf(framerate, sizeof(framerate), "%zu", fps);

        // Execute ffmpeg
        execl("/usr/bin/ffmpeg",
            "ffmpeg",
            "-y",
            "-f", "rawvideo",
            "-vcodec", "rawvideo",
            "-pix_fmt", "rgba",
            "-s", resolution,
            "-r", framerate,
            "-i", "-",
            "-c:v", "libx264",
            "-preset", "ultrafast",
            "-crf", "18",
            "-pix_fmt", "yuv420p",
            "output.mp4",
            NULL
        );

        // If we get here, exec failed
        TraceLog(LOG_ERROR, "FFMPEG CHILD: could not exec ffmpeg: %s", strerror(errno));
        exit(1);
    }

    // In parent process
    close(pipefd[READ_END]);  // Close read end in parent

    FFMPEG *ffmpeg = malloc(sizeof(FFMPEG));
    if (!ffmpeg) {
        close(pipefd[WRITE_END]);
        kill(child, SIGKILL);
        return NULL;
    }

    ffmpeg->pid = child;
    ffmpeg->pipe = pipefd[WRITE_END];
    return ffmpeg;
}

bool ffmpeg_end_rendering(FFMPEG *ffmpeg, bool cancel)
{
    int pipe = ffmpeg->pipe;
    pid_t pid = ffmpeg->pid;

    free(ffmpeg);

    if (close(pipe) < 0) {
        TraceLog(LOG_WARNING, "FFMPEG: could not close write end of the pipe on the parent's end: %s", strerror(errno));
    }

    if (cancel) kill(pid, SIGKILL);

    for (;;) {
        int wstatus = 0;
        if (waitpid(pid, &wstatus, 0) < 0) {
            TraceLog(LOG_ERROR, "FFMPEG: could not wait for ffmpeg child process to finish: %s", strerror(errno));
            return false;
        }

        if (WIFEXITED(wstatus)) {
            int exit_status = WEXITSTATUS(wstatus);
            if (exit_status != 0) {
                TraceLog(LOG_ERROR, "FFMPEG: ffmpeg exited with code %d", exit_status);
                return false;
            }

            return true;
        }

        if (WIFSIGNALED(wstatus)) {
            TraceLog(LOG_ERROR, "FFMPEG: ffmpeg got terminated by %s", strsignal(WTERMSIG(wstatus)));
            return false;
        }
    }

    assert(0 && "unreachable");
}

bool ffmpeg_send_frame_flipped(FFMPEG *ffmpeg, void *data, size_t width, size_t height)
{
    if (!ffmpeg || !data || width == 0 || height == 0) {
        TraceLog(LOG_ERROR, "FFMPEG: Invalid parameters for frame sending");
        return false;
    }

    // Fix orientation by writing rows in normal order and flipping pixels horizontally
    size_t row_size = width * sizeof(uint32_t);
    uint32_t *row_buffer = malloc(row_size);
    if (!row_buffer) {
        TraceLog(LOG_ERROR, "FFMPEG: Failed to allocate row buffer");
        return false;
    }

    // Process each row
    for (size_t y = 0; y < height; y++) {
        uint32_t *src_row = (uint32_t*)data + y * width;
        
        for (size_t x = 0; x < width; x++) {
            // don't flip pixels anymore
           //row_buffer[x] = src_row[width - 1 - x];
              row_buffer[x] = src_row[x];
        }
        size_t bytes_written = 0;
        while (bytes_written < row_size) {
            ssize_t result = write(ffmpeg->pipe, 
                                 (uint8_t*)row_buffer + bytes_written,
                                 row_size - bytes_written);
            
            if (result < 0) {
                if (errno == EINTR) continue;
                free(row_buffer);
                TraceLog(LOG_ERROR, "FFMPEG: failed to write into ffmpeg pipe: %s", strerror(errno));
                return false;
            }
            bytes_written += result;
        }
    }

    free(row_buffer);
    return true;
}
