import argparse
import os

import cv2
import numpy as np
from ultralytics import YOLO


def draw_bounding_boxes(image, boxes, confidences, class_ids, names, colors):
    for box, confidence, class_id in zip(boxes, confidences, class_ids):
        x1, y1, x2, y2 = box

        color = [int(c) for c in colors[class_id]]
        cv2.rectangle(image, (x1, y1), (x2, y2), color, 2)
        text = f"{names[class_id]}: {confidence:.4f}"
        cv2.putText(image, text, (x1, y1 - 5), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

    return image


def make_predictions(image, model, confidence_threshold, nms_threshold, device):
    result = model.predict(image, conf=confidence_threshold, iou=nms_threshold, device=device, verbose=False)[0]

    boxes = result.boxes.xyxy.cpu().numpy().astype(int)
    confidences = result.boxes.conf.cpu().numpy()
    class_ids = result.boxes.cls.cpu().numpy().astype(int)

    return boxes, confidences, class_ids


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-w', '--weights', type=str, default='yolo26n.pt', help='path to model weights (.pt)')
    parser.add_argument('-c', '--confidence', type=float, default=0.5, help='minimum probability to filter weak detections')
    parser.add_argument('-t', '--threshold', type=float, default=0.3, help='IoU threshold for Non-Max Suppression')
    parser.add_argument('-u', '--use_gpu', default=False, action='store_true', help='Use GPU for inference')
    parser.add_argument('-s', '--save', default=False, action='store_true', help='Whether or not the output should be saved')
    parser.add_argument('-sh', '--show', default=True, action="store_false", help='Show output')

    input_group = parser.add_mutually_exclusive_group()
    input_group.add_argument('-i', '--image_path', type=str, default='', help='Path to the image file.')
    input_group.add_argument('-v', '--video_path', type=str, default='', help='Path to the video file.')
    args = parser.parse_args()

    # Load the YOLO model
    model = YOLO(args.weights)
    device = 0 if args.use_gpu else 'cpu'
    if args.use_gpu:
        print("[INFO] Using GPU for inference")

    names = model.names
    colors = np.random.default_rng(42).integers(0, 255, size=(len(names), 3))

    # Save if specified
    if args.save:
        print("[INFO] Output will be saved in the 'output' directory")
        os.makedirs('output', exist_ok=True)

    print("[INFO] YOLO model loaded successfully")

    if args.image_path:
        # Load the image
        image = cv2.imread(args.image_path)

        # Make predictions
        boxes, confidences, class_ids = make_predictions(image, model, args.confidence, args.threshold, device)
        image = draw_bounding_boxes(image, boxes, confidences, class_ids, names, colors)

        # show the output image
        if args.show:
            cv2.imshow('[INFO] YOLO Object Detection', image)
            cv2.waitKey(0)

        if args.save:
            cv2.imwrite(f'output/{args.image_path.split("/")[-1]}', image)
        cv2.destroyAllWindows()
    else:
        if args.video_path != '':
            cap = cv2.VideoCapture(args.video_path)
        else:
            cap = cv2.VideoCapture(0)

        if args.save:
            height = int(cap.get(4))
            width = int(cap.get(3))
            fps = cap.get(cv2.CAP_PROP_FPS)
            print(f"Frames per second: {fps}")
            name = args.video_path.split("/")[-1] if args.video_path else 'camera.avi'
            out = cv2.VideoWriter(f'output/{name}', cv2.VideoWriter_fourcc('M', 'J', 'P', 'G'), fps, (width, height))

        try:
            while cap.isOpened():
                ret, image = cap.read()

                if not ret:
                    print("[INFO] Video is finished")
                    break

                boxes, confidences, class_ids = make_predictions(image, model, args.confidence, args.threshold, device)
                image = draw_bounding_boxes(image, boxes, confidences, class_ids, names, colors)

                if args.show:
                    cv2.imshow('[INFO] YOLO Object Detection', image)
                    if cv2.waitKey(1) & 0xFF == ord('q'):
                        break

                if args.save:
                    out.write(image)
        except KeyboardInterrupt:
            print("\n[INFO] Interrupted, shutting down")
        finally:
            cap.release()
            if args.save:
                out.release()
    cv2.destroyAllWindows()
