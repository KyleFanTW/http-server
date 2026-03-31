# HTTP Server

This repository is a course project for the Computer Networks 2024 of NTU CSIE, instructed by Professor Ai-Chun Pang. In the project, I implemented a small HTTP server and companion client in pure C.

The project includes:
- Static page serving
- File upload and download over HTTP
- Basic Authentication for protected upload endpoints
- Video upload with background conversion to MPEG-DASH using `ffmpeg`
- A browser-based video player powered by Shaka Player
- A simple CLI client for uploading and downloading files

## Features

### Web server
- `GET /` serves the index page
- `GET /file/` shows the uploaded file list
- `GET /video/` shows the uploaded video list
- `GET /upload/file` serves the file upload page and requires Basic Auth
- `GET /upload/video` serves the video upload page and requires Basic Auth
- `POST /api/file` uploads a file and requires Basic Auth
- `POST /api/video` uploads an MP4 video and requires Basic Auth
- `GET /api/file/<filename>` downloads a file
- `GET /api/video/<path>` serves generated DASH assets such as `dash.mpd`, `.m4s`, and audio segments
- `GET /video/<name>` serves the video playback page

### Video pipeline
- Uploaded videos are first stored in `web/tmp/`
- A detached thread invokes `ffmpeg`
- The server generates DASH output under `web/videos/<video-name>/`
- The player page loads the generated manifest with Shaka Player

### CLI client
- `put <file>` uploads a file
- `get <file>` downloads a file into `./files/`
- `putv <file>` uploads a video
- `quit` exits the client

## Project Structure

```text
.
├── Dockerfile
├── docker-compose.yml
├── README.md
└── hw2
    ├── client.c
    ├── server.c
    ├── makefile
    ├── utils
    │   ├── base64.c
    │   └── base64.h
    └── web
        ├── index.html
        ├── listf.rhtml
        ├── listv.rhtml
        ├── player.rhtml
        ├── uploadf.html
        └── uploadv.html
```

## Build

### Local build

```bash
cd hw2
make
```

This produces:
- `./server`
- `./client`

### Docker

The repository includes a containerized environment with the tools needed for compilation and video conversion.

```bash
docker compose up -d
docker compose exec main bash
cd /home/cn/hw2
make
```

If your Docker setup uses the older Compose command, use `docker-compose` instead.

## Run

### Start the server

```bash
cd hw2
./server 8080
```

Then open:

```text
http://localhost:8080
```

### Authentication setup

Protected endpoints read valid credentials from a local file named `secret` in the `hw2/` directory.

Create it like this:

```bash
cd hw2
printf "demo:demo\n" > secret
```

You can add multiple valid credentials, one `username:password` pair per line.

## Client Usage

Run the client with:

```bash
cd hw2
./client <host> <port> [username:password]
```

Example:

```bash
./client 127.0.0.1 8080 demo:demo
```

Example interactive session:

```text
> put notes.txt
> get notes.txt
> putv sample.mp4
> quit
```

Downloaded files are saved to `hw2/files/`.

## Web Usage

After the server starts:

1. Visit `http://localhost:8080`
2. Open the file list or video list
3. Use the protected upload page with valid Basic Auth credentials
4. Upload an MP4 video
5. Wait for background DASH conversion to finish
6. Open the generated player page from the video list




## Acknowledgements

Parts of the codebase are provided as skeletons by the course instructor. You may observe the original skeleton code in the commit history [25963bc](https://github.com/KyleFanTW/http-server/commit/25963bca7bb19c35e4b5e29ca4c6c9a0f62ebb05). I built upon this foundation to implement the required features.

### Generative AI Usage Declaration

This README was generated with the assistance of generative AI tools. However, all remaining code and content in this repository were human- written and reviewed.
