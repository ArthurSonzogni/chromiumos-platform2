// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {parseMessages} from '@parallax/data/parse';

let timerId: any;

/**
 * Send and receive objects between the server.
 *
 * Uses JSON and POST methods to serialize a message and submit it to
 * the server. Deserializes the response from the server and returns it.
 *
 * @param server Server address and port.
 * @param sendData JSON serializable object passed to the server.
 * @return Deserialized JSON response from the server.
 */
async function transfer(server: string, sendData: any) {
  const sendText = JSON.stringify(sendData);
  const response = await fetch(server, {
    method: 'POST',
    mode: 'cors',
    cache: 'no-cache',
    headers: {'Content-Type': 'text/plain'},
    body: sendText,
  });
  const text = await response.text();
  return JSON.parse(text);
}

/**
 * Request data from the server and pass the response to the callbacks.
 * @param server Server address and port.
 */
async function getData(server: string) {
  console.log('SERVER', server);
  const data = await transfer(server, {type: 'getData'});
  parseMessages(data);
}

/**
 * Connect and disconnect the client from the streaming server.
 * @param server Server path and port.
 */
export async function toggleClient(server: string) {
  if (timerId !== undefined) {
    clearTimeout(timerId);
    timerId = undefined;
    return;
  }
  timerId = setInterval(() => {
    getData(server);
  }, 1 * 1000);
}
