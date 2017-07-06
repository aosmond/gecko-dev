/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

function getFileAsInputStream(aFilename) {
  var file = Components.classes["@mozilla.org/file/directory_service;1"]
             .getService(Components.interfaces.nsIProperties)
             .get("CurWorkD", Components.interfaces.nsIFile);

  file.append("tests");
  file.append("image");
  file.append("test");
  file.append("mochitest");
  file.append(aFilename);

  var fileStream = Components.classes['@mozilla.org/network/file-input-stream;1']
                   .createInstance(Components.interfaces.nsIFileInputStream);
  fileStream.init(file, 1, 0, false);
  return fileStream;
}

function handleRequest(request, response)
{
  if (request.queryString === "true") {
    response.setHeader("Content-Type", "image/gif", false);
    response.setStatusLine(request.httpVersion, 200, "OK");
    var inputStream = getFileAsInputStream("blue.gif");
    response.bodyOutputStream.writeFrom(inputStream, inputStream.available());
    inputStream.close();
  } else {
    response.setStatusLine(request.httpVersion, 206, "Partial Content");
  }
}

