// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {ParallaxError} from '@parallax/common/error';

/**
 * Checks if a object is an iterable collection of values, Arrays and Sets.
 *
 * @param data Object to check.
 * @return     True if the data is an iterable ArrayLike object.
 */
export function isArrayLike(data: any) {
  if (Array.isArray(data) || data instanceof Set) {
    return true;
  }
  return false;
}

/**
 * Checks if a object is an iterable collection of values, Arrays and Sets.
 * If an object is not array-like then a ParallaxError will be thrown.
 *
 * @param data Object to check.
 * @return     Same object
 */
export function iterableArray(data: any) {
  if (isArrayLike(data)) {
    return data;
  }
  throw new ParallaxError('Not array-like', {'data': data});
}

/**
 * Helper function to iterate over a map-like objects consistently.
 * If an object is not map-like then a ParallaxError will be thrown.
 *
 * @param data  An map-like object we wish to iterate.
 * @return      [key, value] like iterable object.
 */
export function iterableMap(data: any) {
  if (!isArrayLike(data)) {
    if (data instanceof Map) {
      return data;
    } else if (data.constructor === Object.prototype.constructor) {
      return Object.entries(data);
    }
  }
  throw new ParallaxError('Not map-like', {'data': data});
}

/**
 * Sorts a Array of text strings with a natural numeric sort.
 *
 * @param text Input text array.
 * @return Sorted array using a natural numeric sort.
 */
function naturalSort(text: string[]) {
  text = [...text];
  let numeric = new Intl.Collator(undefined, {numeric: true});
  return text.sort(numeric.compare);
}

/**
 * Implements a subset of the strftime spec to produce timestamp strings.
 * By default it will generate a timestamp string with the current local time.
 *
 * Supported format codes:
 *   2 digit date: %y, %m, %d
 *   2 digit time: %H, %M, %S
 *   Escape: %%
 *
 * @param format    Timestamp format string.
 * @param opts      Optional named arguments.
 * @param opts.date Replaces the Date with a provided Date object.
 * @param opts.utc  When true converts the local timestamp to UTC timestamp.
 * @return          Timestamp String.
 */
export function strftime(format: string, opts?: {date?: Date, utc?: boolean}) {
  let date = opts?.date ? opts.date : new Date();
  if (opts?.utc) {
    date = new Date(date.getTime() + date.getTimezoneOffset() * 60000);
  }
  const codes = {
    '%Y': String(date.getFullYear()),
    '%y': String(date.getFullYear() % 100).padStart(2, '0'),
    '%m': String(date.getMonth() + 1).padStart(2, '0'),
    '%d': String(date.getDate()).padStart(2, '0'),
    '%H': String(date.getHours()).padStart(2, '0'),
    '%M': String(date.getMinutes()).padStart(2, '0'),
    '%S': String(date.getSeconds()).padStart(2, '0'),
  };
  let parsed = format;
  for (const [key, value] of Object.entries(codes)) {
    const regex = new RegExp('(?<!%)' + key, 'g');
    parsed = parsed.replaceAll(regex, value);
  }
  // Check for invalid flags by searching for any odd number of % characters.
  const invalidFlags = parsed.match(/(?<!%)(?:(%%)*)%($|[^%])/g);
  if (invalidFlags) {
    throw new ParallaxError(
        'Invalid Flags found',
        {'format': format, 'parsed': parsed, 'invalidFlags': invalidFlags});
  }
  // Replace all of the even matchings.
  parsed = parsed.replaceAll('%%', '%');
  return parsed;
}
