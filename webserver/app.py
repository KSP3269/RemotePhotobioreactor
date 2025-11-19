from flask import Flask, render_template, Response
import cv2 as cv
import time

app = Flask(__name__)

# Initialize camera (0 is usually the default camera)
print ("Attempting to open camera..")
camera = cv.VideoCapture(0)

if not camera.isOpened():
    print("ERROR: Could not open camera")
else:
    print("Camera successfully opened")
    camera.set(cv.CAP_PROP_FRAME_WIDTH,640)
    camera.set(cv.CAP_PROP_FRAME_HEIGHT,480)
    camera.set(cv.CAP_PROP_FPS,30)

def generate_frames():
    """Generator function to continuously capture and yield frames"""
    frame_count=0
    while True:
        success, frame = camera.read()
        if not success:
            print(f"ERROR: Failed to read frame {frame_count}")
            time.sleep(0.1)
            continue
        frame_count += 1
        if frame_count % 30==0:
            print(f"Successfully streamed {frame_count} frames")
        else:
            # Encode frame as JPEG
            ret, buffer = cv.imencode('.jpg', frame)
            if not ret:
                print("ERROR: Failed to encode frame")
                continue

            frame = buffer.tobytes()
            
            # Yield frame in multipart format
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')
            time.sleep(0.033)

@app.route('/')
def index():
    """Main page with camera feed"""
    return render_template('index.html')

@app.route('/video_feed')
def video_feed():
    """Route for video streaming"""
    return Response(generate_frames(),mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == '__main__':
    print("Starting Plant Monitor server...")

    print("Open your browser and go to: http://localhost:5000")
    app.run(host='0.0.0.0', port=5000, debug=True, use_reloader=False)
