#include "nas_disk_sort.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static nas_disk_sort_key_t s_sort_key;
static bool s_descending;

static int sign(int value)
{
    return (value > 0) - (value < 0);
}

static int ascii_case_compare(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        const int left_char = tolower((unsigned char)*left);
        const int right_char = tolower((unsigned char)*right);
        if (left_char != right_char) return left_char < right_char ? -1 : 1;
        left++;
        right++;
    }
    return sign((unsigned char)*left - (unsigned char)*right);
}

static int natural_case_compare(const char *left, const char *right)
{
    const char *left_start = left;
    const char *right_start = right;
    while (*left != '\0' && *right != '\0') {
        if (isdigit((unsigned char)*left) && isdigit((unsigned char)*right)) {
            while (*left == '0') left++;
            while (*right == '0') right++;
            const char *left_end = left;
            const char *right_end = right;
            while (isdigit((unsigned char)*left_end)) left_end++;
            while (isdigit((unsigned char)*right_end)) right_end++;
            const size_t left_digits = (size_t)(left_end - left);
            const size_t right_digits = (size_t)(right_end - right);
            if (left_digits != right_digits) return left_digits < right_digits ? -1 : 1;
            const int digit_compare = memcmp(left, right, left_digits);
            if (digit_compare != 0) return sign(digit_compare);
            left = left_end;
            right = right_end;
            continue;
        }
        const int left_char = tolower((unsigned char)*left);
        const int right_char = tolower((unsigned char)*right);
        if (left_char != right_char) return left_char < right_char ? -1 : 1;
        left++;
        right++;
    }
    const int length_compare = sign((unsigned char)*left - (unsigned char)*right);
    return length_compare != 0 ? length_compare : ascii_case_compare(left_start, right_start);
}

static int compare_populated_text(const char *left, const char *right, bool natural)
{
    if (left[0] == '\0' || right[0] == '\0') {
        if (left[0] == right[0]) return 0;
        return left[0] == '\0' ? 1 : -1;
    }
    return natural ? natural_case_compare(left, right) : ascii_case_compare(left, right);
}

static int compare_disks(const void *left_pointer, const void *right_pointer)
{
    const nas_disk_t *left = left_pointer;
    const nas_disk_t *right = right_pointer;
    int result = 0;

    if (s_sort_key == NAS_DISK_SORT_ID) {
        if ((left->id[0] == '\0') != (right->id[0] == '\0'))
            return left->id[0] == '\0' ? 1 : -1;
        result = compare_populated_text(left->id, right->id, true);
    } else if (s_sort_key == NAS_DISK_SORT_MODEL) {
        if ((left->model[0] == '\0') != (right->model[0] == '\0'))
            return left->model[0] == '\0' ? 1 : -1;
        result = compare_populated_text(left->model, right->model, false);
    } else {
        if (left->temperature_valid != right->temperature_valid)
            return left->temperature_valid ? -1 : 1;
        if (left->temperature_valid && left->temperature_c != right->temperature_c)
            result = left->temperature_c < right->temperature_c ? -1 : 1;
    }

    if (result != 0) return s_descending ? -result : result;
    return compare_populated_text(left->id, right->id, true);
}

void nas_disk_sort(nas_disk_t *disks, size_t count, nas_disk_sort_key_t key,
                   bool descending)
{
    if (disks == NULL || count < 2) return;
    s_sort_key = key;
    s_descending = descending;
    qsort(disks, count, sizeof(*disks), compare_disks);
}
