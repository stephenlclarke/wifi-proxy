#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-/dev/cu.usbmodem1101}"
BACKUP_DIR="${BACKUP_DIR:-backups}"
FLASH_SIZE_BYTES="${FLASH_SIZE_BYTES:-16777216}"
CHUNK_SIZE_BYTES="${CHUNK_SIZE_BYTES:-1048576}"
RETRIES="${RETRIES:-3}"

# Wait for the USB CDC serial device to return after ESP32-S3 resets.
wait_for_port() {
    while [[ ! -e "${PORT}" ]]; do
        printf 'Waiting for %s\n' "${PORT}"
        sleep 2
    done
}

# Read one flash chunk with retries so transient USB resets do not lose the full backup.
read_chunk() {
    local index="$1"
    local offset="$2"
    local size="$3"
    local chunk_file="$4"
    local manifest_file="$5"
    local offset_hex
    local size_hex
    local tmp_file
    local attempt
    local actual_size

    offset_hex="$(printf '0x%06x' "${offset}")"
    size_hex="$(printf '0x%x' "${size}")"
    tmp_file="${chunk_file}.tmp"

    attempt=1
    while ((attempt <= RETRIES)); do
        wait_for_port
        printf 'Reading chunk %02d at %s (%s bytes), attempt %d\n' \
            "${index}" "${offset_hex}" "${size}" "${attempt}" | tee -a "${manifest_file}"
        rm -f "${tmp_file}"

        if esptool --port "${PORT}" read-flash "${offset_hex}" "${size_hex}" "${tmp_file}" \
            >> "${manifest_file}" 2>&1; then
            actual_size="$(wc -c < "${tmp_file}" | tr -d ' ')"
            if [[ "${actual_size}" == "${size}" ]]; then
                mv "${tmp_file}" "${chunk_file}"
                printf 'Chunk %02d complete (%s bytes)\n' "${index}" "${actual_size}" \
                    | tee -a "${manifest_file}"
                return 0
            fi

            printf 'Chunk %02d wrong size: %s bytes\n' "${index}" "${actual_size}" \
                | tee -a "${manifest_file}"
        else
            printf 'Chunk %02d attempt %d failed\n' "${index}" "${attempt}" \
                | tee -a "${manifest_file}"
        fi

        attempt=$((attempt + 1))
        sleep 2
    done

    printf 'Failed to read chunk %02d after %d attempts\n' "${index}" "${RETRIES}" \
        | tee -a "${manifest_file}"
    return 1
}

main() {
    local timestamp
    local chunk_dir
    local full_file
    local manifest_file
    local chunk_count
    local index
    local offset
    local remaining
    local size
    local offset_hex
    local chunk_file
    local actual_size
    local sha

    mkdir -p "${BACKUP_DIR}"
    timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
    chunk_dir="${BACKUP_DIR}/lilygo-t-display-s3-${timestamp}-chunks"
    full_file="${BACKUP_DIR}/lilygo-t-display-s3-${timestamp}-full-flash-16mb.bin"
    manifest_file="${BACKUP_DIR}/lilygo-t-display-s3-${timestamp}-full-flash-16mb.txt"
    mkdir -p "${chunk_dir}"

    {
        printf 'Created: %s\n' "${timestamp}"
        printf 'Port: %s\n' "${PORT}"
        printf 'Flash size bytes: %s\n' "${FLASH_SIZE_BYTES}"
        printf 'Chunk size bytes: %s\n' "${CHUNK_SIZE_BYTES}"
        printf 'Retries per chunk: %s\n\n' "${RETRIES}"
    } > "${manifest_file}"

    chunk_count=$(((FLASH_SIZE_BYTES + CHUNK_SIZE_BYTES - 1) / CHUNK_SIZE_BYTES))
    for ((index = 0; index < chunk_count; index++)); do
        offset=$((index * CHUNK_SIZE_BYTES))
        remaining=$((FLASH_SIZE_BYTES - offset))
        size="${CHUNK_SIZE_BYTES}"
        if ((remaining < CHUNK_SIZE_BYTES)); then
            size="${remaining}"
        fi

        offset_hex="$(printf '0x%06x' "${offset}")"
        chunk_file="$(printf '%s/chunk-%02d-%s.bin' "${chunk_dir}" "${index}" "${offset_hex}")"
        read_chunk "${index}" "${offset}" "${size}" "${chunk_file}" "${manifest_file}"
    done

    : > "${full_file}"
    for ((index = 0; index < chunk_count; index++)); do
        offset=$((index * CHUNK_SIZE_BYTES))
        offset_hex="$(printf '0x%06x' "${offset}")"
        chunk_file="$(printf '%s/chunk-%02d-%s.bin' "${chunk_dir}" "${index}" "${offset_hex}")"
        cat "${chunk_file}" >> "${full_file}"
    done

    actual_size="$(wc -c < "${full_file}" | tr -d ' ')"
    sha="$(shasum -a 256 "${full_file}" | awk '{print $1}')"

    {
        printf '\nBackup file: %s\n' "${full_file}"
        printf 'Size bytes: %s\n' "${actual_size}"
        printf 'SHA256: %s\n' "${sha}"
    } | tee -a "${manifest_file}"

    if [[ "${actual_size}" != "${FLASH_SIZE_BYTES}" ]]; then
        printf 'Backup size mismatch: expected %s, got %s\n' \
            "${FLASH_SIZE_BYTES}" "${actual_size}" >&2
        return 1
    fi
}

main "$@"
